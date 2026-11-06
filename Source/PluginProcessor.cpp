// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
#include "PluginProcessor.h"
#include "PluginEditor.h"

TablarionProcessor::TablarionProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "Tablarion", createLayout())
{
    masterParam = params.getRawParameterValue ("master");
    reverbParam = params.getRawParameterValue ("reverb");
    speedParam  = params.getRawParameterValue ("speed");
    autoLoadDefaultSoundFont();
}

TablarionProcessor::~TablarionProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout TablarionProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master Volume", NormalisableRange<float> (0.0f, 1.5f), 0.85f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverb", 1 }, "Reverb", NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Playback Speed", NormalisableRange<float> (0.25f, 2.0f, 0.01f), 1.0f));
    return l;
}

bool TablarionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void TablarionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool changed = sampleRate != currentSampleRate || samplesPerBlock > currentBlockSize;
    currentSampleRate = sampleRate;
    currentBlockSize = std::max (currentBlockSize, samplesPerBlock);
    scratch.setSize (2, currentBlockSize, false, false, true);
    if (changed || engine == nullptr) rebuildEngine();
}

void TablarionProcessor::releaseResources() {}

void TablarionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    controls.masterGain = masterParam->load();
    controls.reverbAmount = reverbParam->load();
    controls.speed = speedParam->load();

    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked() || engine == nullptr)
    {
        buffer.clear();
        return;
    }

    if (followHost.load())
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                engine->followHost (pos->getIsPlaying(), pos->getPpqPosition().orFallback (0.0), pos->getBpm().orFallback (0.0));

    if (scratch.getNumSamples() < n) { buffer.clear(); return; } // host exceeded announced block size

    int done = 0;
    while (done < n)
    {
        const int len = std::min (n - done, currentBlockSize);
        juce::MidiBuffer slice;
        for (const auto meta : midi)
            if (meta.samplePosition >= done && meta.samplePosition < done + len)
                slice.addEvent (meta.getMessage(), meta.samplePosition - done);
        engine->process (scratch.getWritePointer (0), scratch.getWritePointer (1), len, &slice, &transport);
        if (buffer.getNumChannels() >= 2)
        {
            buffer.copyFrom (0, done, scratch, 0, 0, len);
            buffer.copyFrom (1, done, scratch, 1, 0, len);
        }
        else if (buffer.getNumChannels() == 1)
        {
            buffer.clear (0, done, len);
            buffer.addFrom (0, done, scratch, 0, 0, len, 0.5f);
            buffer.addFrom (0, done, scratch, 1, 0, len, 0.5f);
        }
        done += len;
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

// ------------------------------------------------------------------------------------------------

bool TablarionProcessor::loadScoreFile (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) { error = "Cannot read " + file.getFileName(); return false; }
    if (! loadScoreData (mb, file.getFileNameWithoutExtension(), error)) return false;
    scorePath = file.getFullPathName();
    return true;
}

bool TablarionProcessor::loadScoreData (const juce::MemoryBlock& data, const juce::String& name, juce::String& error)
{
    auto s = tc::loadGuitarPro (data.getData(), data.getSize(), error);
    if (! s) return false;
    if (s->title.empty()) s->title = name.toStdString();

    // stop playback before swapping songs
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        if (engine) engine->stop (true);
    }
    transport.position = 0;
    transport.playing = false;

    controls.resetTracks();
    for (size_t i = 0; i < s->tracks.size() && i < (size_t) tc::kMaxTracks; ++i)
    {
        controls.volume[i] = std::clamp (s->tracks[i].volume, 0.05f, 1.0f);
        controls.pan[i] = s->tracks[i].pan;
    }
    controls.liveTrack = 0;
    score = std::shared_ptr<const tc::Score> (std::move (s));
    scoreData = data;
    scorePath.clear();
    recompile();
    status = juce::String (score->formatName) + " loaded";
    sendChangeMessage();
    return true;
}

void TablarionProcessor::closeScore()
{
    if (! score) return;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        if (engine) engine->stop (true);
    }
    transport.command = 0;
    transport.seekTick = -1;
    transport.position = 0;
    transport.playing = false;

    const juce::String closedName = juce::String::fromUTF8 (score->title.c_str());
    score.reset();
    song.reset();
    scoreData.reset();
    scorePath.clear();
    controls.resetTracks();
    controls.liveTrack = 0;
    rebuildEngine(); // empty engine: silence, but SoundFont / settings are kept
    status = closedName.isNotEmpty() ? "Closed: " + closedName : juce::String ("Score closed");
    sendChangeMessage();
}

void TablarionProcessor::recompile()
{
    if (! score) return;
    song = std::shared_ptr<const tc::CompiledSong> (tc::compileScore (*score, compileOptions));
    rebuildEngine();
}

void TablarionProcessor::rebuildEngine()
{
    if (currentSampleRate <= 0.0) return;
    auto fresh = std::make_unique<tc::SongEngine> (score, song, soundFont, soundMode, controls, (float) currentSampleRate, currentBlockSize);

    int64_t pos = transport.position.load();
    const bool wasPlaying = transport.playing.load();
    if (song) fresh->seek (std::min<int64_t> (pos, song->lengthTicks));
    if (wasPlaying) fresh->play();

    std::unique_ptr<tc::SongEngine> old;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        old = std::move (engine);
        engine = std::move (fresh);
    }
    old.reset(); // destroyed outside the audio lock, on the message thread
}

juce::String TablarionProcessor::getEngineName (int track) const
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    return engine ? engine->trackEngineName (track) : juce::String();
}

void TablarionProcessor::seekBar (int playIndex)
{
    if (! song || song->bars.empty()) return;
    playIndex = std::clamp (playIndex, 0, (int) song->bars.size() - 1);
    seekTick (song->bars[(size_t) playIndex].tick);
}

void TablarionProcessor::setSoundMode (tc::SoundMode m)
{
    soundMode = m;
    rebuildEngine();
    sendChangeMessage();
}

void TablarionProcessor::setTrackEngine (int track, tc::TrackEngine e)
{
    if (track < 0 || track >= tc::kMaxTracks) return;
    controls.trackEngine[track] = (int) e;
    rebuildEngine();
    sendChangeMessage();
}

void TablarionProcessor::setHumanize (float h)
{
    compileOptions.humanize = juce::jlimit (0.0f, 1.0f, h);
    recompile();
}

void TablarionProcessor::setAutoStrum (bool b)
{
    compileOptions.autoStrum = b;
    recompile();
}

void TablarionProcessor::loadSoundFontAsync (const juce::File& file)
{
    status = "Loading " + file.getFileName() + "...";
    sendChangeMessage();
    std::weak_ptr<bool> alive = aliveToken;
    std::thread ([this, file, alive]
    {
        juce::String err;
        auto sf = tc::SoundFont::load (file, err);
        juce::MessageManager::callAsync ([this, sf, err, file, alive]
        {
            if (alive.expired()) return;
            if (sf)
            {
                soundFont = sf;
                soundFontPath = file.getFullPathName();
                if (soundMode == tc::SoundMode::Modeled) soundMode = tc::SoundMode::Hybrid;
                status = "SoundFont: " + juce::String (sf->name);
                rebuildEngine();
            }
            else status = "SoundFont error: " + err;
            sendChangeMessage();
        });
    }).detach();
}

void TablarionProcessor::clearSoundFont()
{
    soundFont.reset();
    soundFontPath.clear();
    soundMode = tc::SoundMode::Modeled;
    rebuildEngine();
    sendChangeMessage();
}

juce::StringArray TablarionProcessor::getRecentSoundFontCandidates() const
{
    juce::StringArray out;
    const juce::File dirs[] = {
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Sounds/Banks"),
        juce::File ("/Library/Audio/Sounds/Banks"),
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Tablarion/SoundFonts"),
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Tablarion/SoundFonts")
    };
    for (auto& d : dirs)
        if (d.isDirectory())
            for (auto& f : d.findChildFiles (juce::File::findFiles, false, "*.sf2"))
                out.add (f.getFullPathName());
    return out;
}

void TablarionProcessor::autoLoadDefaultSoundFont()
{
    // Nothing is loaded automatically unless the user put a bank in a Tablarion folder.
    const auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Tablarion/SoundFonts");
    if (! dir.isDirectory()) return;
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.sf2");
    if (! files.isEmpty())
    {
        juce::String err;
        if (auto sf = tc::SoundFont::load (files.getFirst(), err))
        {
            soundFont = sf;
            soundFontPath = files.getFirst().getFullPathName();
            soundMode = tc::SoundMode::Hybrid;
        }
    }
}

// ------------------------------------------------------------------------------------------------

void TablarionProcessor::exportWavAsync (const juce::File& file, double sampleRate, std::function<void (bool, juce::String)> done)
{
    if (! score || ! song) { done (false, "No score loaded"); return; }
    auto sc = score; auto sg = song; auto sf = soundFont; auto mode = soundMode;
    auto ctl = std::make_shared<tc::EngineControls>();
    ctl->copyFrom (controls);
    ctl->speed = 1.0f;
    ctl->loop = false;
    auto progressState = exportState;
    progressState->store (0.0f);

    std::weak_ptr<bool> alive = aliveToken;
    std::thread ([file, sampleRate, done, sc, sg, sf, mode, ctl, alive, progressState]
    {
        auto progress = [progressState] (float v) { progressState->store (v); };
        const int block = 1024;
        tc::SongEngine eng (sc, sg, sf, mode, *ctl, (float) sampleRate, block);
        eng.play();
        const double tail = 3.0;
        const int64_t total = (int64_t) ((sg->lengthSeconds + tail) * sampleRate);

        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        bool ok = stream != nullptr;
        juce::String msg;
        if (ok)
        {
            juce::WavAudioFormat wav;
            auto opts = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24);
            std::unique_ptr<juce::OutputStream> os (stream.release());
            auto writer = wav.createWriterFor (os, opts);
            if (writer)
            {
                juce::AudioBuffer<float> buf (2, block);
                for (int64_t pos = 0; pos < total; pos += block)
                {
                    const int n = (int) std::min<int64_t> (block, total - pos);
                    eng.process (buf.getWritePointer (0), buf.getWritePointer (1), n, nullptr, nullptr);
                    writer->writeFromAudioSampleBuffer (buf, 0, n);
                    progress ((float) pos / (float) total);
                }
                msg = "Exported " + file.getFileName();
            }
            else { ok = false; msg = "Cannot create WAV writer"; }
        }
        else msg = "Cannot write " + file.getFullPathName();

        progress (-1.0f);
        juce::MessageManager::callAsync ([done, ok, msg, alive] { if (! alive.expired()) done (ok, msg); });
    }).detach();
}

// ------------------------------------------------------------------------------------------------

void TablarionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("TablarionState");
    state.setProperty ("version", 1, nullptr);
    state.setProperty ("scorePath", scorePath, nullptr);
    if (scoreData.getSize() > 0) state.setProperty ("scoreData", scoreData.toBase64Encoding(), nullptr);
    state.setProperty ("soundFont", soundFontPath, nullptr);
    state.setProperty ("soundMode", (int) soundMode, nullptr);
    state.setProperty ("humanize", compileOptions.humanize, nullptr);
    state.setProperty ("autoStrum", compileOptions.autoStrum, nullptr);
    state.setProperty ("followHost", followHost.load(), nullptr);
    state.setProperty ("loop", controls.loop.load(), nullptr);
    state.setProperty ("liveTrack", controls.liveTrack.load(), nullptr);

    juce::ValueTree mix ("Mixer");
    const int nt = score ? std::min ((int) score->tracks.size(), tc::kMaxTracks) : 0;
    for (int i = 0; i < nt; ++i)
    {
        juce::ValueTree t ("Track");
        t.setProperty ("vol", controls.volume[i].load(), nullptr);
        t.setProperty ("pan", controls.pan[i].load(), nullptr);
        t.setProperty ("mute", controls.mute[i].load(), nullptr);
        t.setProperty ("solo", controls.solo[i].load(), nullptr);
        t.setProperty ("engine", controls.trackEngine[i].load(), nullptr);
        mix.appendChild (t, nullptr);
    }
    state.appendChild (mix, nullptr);
    state.appendChild (params.copyState(), nullptr);

    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void TablarionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (! xml) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType ("TablarionState")) return;

    if (auto p = state.getChildWithName (params.state.getType()); p.isValid()) params.replaceState (p);

    compileOptions.humanize = (float) state.getProperty ("humanize", 0.15f);
    compileOptions.autoStrum = (bool) state.getProperty ("autoStrum", true);
    followHost = (bool) state.getProperty ("followHost", false);
    controls.loop = (bool) state.getProperty ("loop", false);
    soundMode = (tc::SoundMode) (int) state.getProperty ("soundMode", 0);

    const juce::String sfPath = state.getProperty ("soundFont", "").toString();
    if (sfPath.isNotEmpty() && juce::File (sfPath).existsAsFile() && sfPath != soundFontPath)
    {
        juce::String err;
        if (auto sf = tc::SoundFont::load (juce::File (sfPath), err)) { soundFont = sf; soundFontPath = sfPath; }
    }
    if (! soundFont && soundMode != tc::SoundMode::Modeled) soundMode = tc::SoundMode::Modeled;

    juce::MemoryBlock mb;
    const juce::String b64 = state.getProperty ("scoreData", "").toString();
    const juce::String path = state.getProperty ("scorePath", "").toString();
    juce::String err;
    bool loaded = false;
    if (b64.isNotEmpty() && mb.fromBase64Encoding (b64)) loaded = loadScoreData (mb, juce::File (path).getFileNameWithoutExtension(), err);
    else if (path.isNotEmpty() && juce::File (path).existsAsFile()) loaded = loadScoreFile (juce::File (path), err);
    if (loaded) scorePath = path;

    auto mix = state.getChildWithName ("Mixer");
    for (int i = 0; i < mix.getNumChildren() && i < tc::kMaxTracks; ++i)
    {
        auto t = mix.getChild (i);
        controls.volume[i] = (float) t.getProperty ("vol", 0.8f);
        controls.pan[i] = (float) t.getProperty ("pan", 0.5f);
        controls.mute[i] = (bool) t.getProperty ("mute", false);
        controls.solo[i] = (bool) t.getProperty ("solo", false);
        controls.trackEngine[i] = (int) t.getProperty ("engine", 0);
    }
    controls.liveTrack = (int) state.getProperty ("liveTrack", 0);
    rebuildEngine();
    sendChangeMessage();
}

juce::AudioProcessorEditor* TablarionProcessor::createEditor() { return new TablarionEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TablarionProcessor(); }
