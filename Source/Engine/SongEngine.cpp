// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
#include "SongEngine.h"
#include "SoundFont.h"

namespace tc
{
SongEngine::SongEngine (std::shared_ptr<const Score> sc, std::shared_ptr<const CompiledSong> sg,
                        std::shared_ptr<const SoundFont> sf, SoundMode mode, EngineControls& c,
                        float sampleRate, int maxBlockSize)
    : score (std::move (sc)), song (std::move (sg)), soundFont (std::move (sf)), controls (c),
      sr (sampleRate), maxBlock (std::max (32, maxBlockSize))
{
    const int nt = score ? std::min ((int) score->tracks.size(), kMaxTracks) : 0;
    tracks.resize ((size_t) nt);
    for (int i = 0; i < nt; ++i)
    {
        const Track& t = score->tracks[(size_t) i];
        auto& slot = tracks[(size_t) i];
        const auto choice = (TrackEngine) controls.trackEngine[i].load();

        bool wantSF = false;
        if (soundFont)
        {
            if (choice == TrackEngine::SoundFont) wantSF = true;
            else if (choice == TrackEngine::Auto)
            {
                if (mode == SoundMode::SoundFont) wantSF = true;
                else if (mode == SoundMode::Hybrid) wantSF = ! (isGuitarFamily (t.family) || isBassFamily (t.family));
            }
        }
        if (wantSF) slot.inst = makeSoundFontInstrument (soundFont, t);
        if (! slot.inst) slot.inst = makeModeledInstrument (t);
        slot.engine = slot.inst->engineName();
        slot.inst->prepare (sr);
        // sampled (non-guitar) instruments already carry their own colour: skip family-specific processing
        const bool sampled = slot.engine == "SoundFont";
        const bool keepChain = isGuitarFamily (t.family) || isBassFamily (t.family);
        slot.fx.prepare (sr, maxBlock, sampled && ! keepChain ? Family::Other : t.family);
        slot.L.assign ((size_t) maxBlock, 0.0f);
        slot.R.assign ((size_t) maxBlock, 0.0f);
    }

    sendL.assign ((size_t) maxBlock, 0.0f); sendR.assign ((size_t) maxBlock, 0.0f);
    wetL.assign ((size_t) maxBlock, 0.0f);  wetR.assign ((size_t) maxBlock, 0.0f);
    reverb.prepare (sr);
    reverb.setParams (2.3f, 1.0f, 6500.0f, 16.0f);
    glue.prepare (sr);
    glue.set (-14.0f, 1.8f, 25.0f, 220.0f, 1.5f);
    limiter.prepare (sr);
    limiter.setCeiling (-0.5f);
}

juce::String SongEngine::trackEngineName (int i) const
{
    return i >= 0 && i < (int) tracks.size() ? tracks[(size_t) i].engine : juce::String();
}

void SongEngine::play()
{
    if (! song) return;
    if (tick >= (double) song->lengthTicks) seek (0);
    playing = true;
}

void SongEngine::stop (bool hard)
{
    playing = false;
    for (auto& t : tracks) t.inst->allNotesOff (hard);
}

void SongEngine::seek (int64_t newTick)
{
    if (! song) return;
    newTick = std::clamp<int64_t> (newTick, 0, song->lengthTicks);
    tick = (double) newTick;
    auto it = std::lower_bound (song->events.begin(), song->events.end(), newTick,
                                [] (const NoteEvent& e, int64_t t) { return e.tick < t; });
    nextEvent = (size_t) std::distance (song->events.begin(), it);
    for (auto& t : tracks) t.inst->allNotesOff (true);
}

void SongEngine::followHost (bool hostPlaying, double ppq, double hostBpm)
{
    if (! song) return;
    if (hostPlaying)
    {
        const double hostTick = std::max (0.0, ppq * PPQ);
        if (! hostWasPlaying || std::abs (hostTick - tick) > PPQ / 2.0)
            seek ((int64_t) hostTick);
        playing = hostTick < (double) song->lengthTicks;
        hostSpeed = hostBpm > 0.0 ? hostBpm : 0.0;
    }
    else if (hostWasPlaying)
    {
        stop (false);
        hostSpeed = 0.0;
    }
    hostWasPlaying = hostPlaying;
}

void SongEngine::dispatchUntil (double endTick, double secPerTick)
{
    const auto& evs = song->events;
    while (nextEvent < evs.size() && (double) evs[nextEvent].tick < endTick)
    {
        const auto& e = evs[nextEvent++];
        if (e.track >= 0 && e.track < (int) tracks.size())
            tracks[(size_t) e.track].inst->noteOn (e, secPerTick);
    }
}

void SongEngine::handleMidi (const juce::MidiMessage& m)
{
    const int lt = controls.liveTrack.load();
    if (lt < 0 || lt >= (int) tracks.size()) return;
    auto& inst = *tracks[(size_t) lt].inst;
    if (m.isNoteOn())
    {
        NoteEvent e;
        e.track = lt;
        e.pitch = (float) m.getNoteNumber();
        e.velocity = m.getFloatVelocity();
        e.durTicks = PPQ * 1000;
        e.curveTicks = PPQ;
        e.flags = EvLive;
        e.string = -1;
        inst.noteOn (e, 60.0 / (120.0 * PPQ));
    }
    else if (m.isNoteOff()) inst.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel()) inst.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f);
    else if (m.isAllNotesOff() || m.isAllSoundOff()) inst.allNotesOff (m.isAllSoundOff());
}

void SongEngine::process (float* L, float* R, int n, const juce::MidiBuffer* midi, TransportRequests* tr)
{
    if (tr)
    {
        const int64_t seekReq = tr->seekTick.exchange (-1);
        if (seekReq >= 0) seek (seekReq);
        switch (tr->command.exchange (0))
        {
            case 1: play(); break;
            case 2: stop (false); break;
            case 3: if (playing) stop (false); else play(); break;
            default: break;
        }
    }

    const int nt = (int) tracks.size();
    for (auto& t : tracks)
    {
        std::fill (t.L.begin(), t.L.begin() + n, 0.0f);
        std::fill (t.R.begin(), t.R.begin() + n, 0.0f);
    }

    int pos = 0;
    while (pos < n)
    {
        const int chunk = std::min (32, n - pos);

        if (midi)
            for (const auto meta : *midi)
                if (meta.samplePosition >= pos && meta.samplePosition < pos + chunk)
                    handleMidi (meta.getMessage());

        if (playing && song)
        {
            double bpm = song->tempo.bpmAt ((int64_t) tick);
            if (hostSpeed > 0.0) bpm = hostSpeed;
            else bpm *= std::clamp ((double) controls.speed.load(), 0.1, 4.0);
            const double ticksPerSample = bpm / 60.0 * PPQ / sr;
            const double secPerTick = 60.0 / (bpm * PPQ);
            const double endTick = tick + chunk * ticksPerSample;
            dispatchUntil (endTick, secPerTick);
            tick = endTick;
            if (tick >= (double) song->lengthTicks)
            {
                if (controls.loop.load() && hostSpeed <= 0.0) { seek (0); }
                else playing = false;
            }
        }

        for (auto& t : tracks)
            t.inst->render (t.L.data() + pos, t.R.data() + pos, chunk);
        pos += chunk;
    }

    // mixer
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    std::fill (sendL.begin(), sendL.begin() + n, 0.0f);
    std::fill (sendR.begin(), sendR.begin() + n, 0.0f);

    bool anySolo = false;
    for (int i = 0; i < nt; ++i) anySolo |= controls.solo[i].load();

    for (int i = 0; i < nt; ++i)
    {
        auto& t = tracks[(size_t) i];
        t.fx.process (t.L.data(), t.R.data(), n);
        const bool audible = ! controls.mute[i].load() && (! anySolo || controls.solo[i].load());
        const float vol = audible ? controls.volume[i].load() : 0.0f;
        const float g = vol * vol * 1.6f; // perceptual fader law
        const float pan = std::clamp (controls.pan[i].load(), 0.0f, 1.0f);
        const float gl = g * std::min (1.0f, 2.0f * (1.0f - pan));
        const float gr = g * std::min (1.0f, 2.0f * pan);
        const float send = t.fx.reverbSend();
        float peak = 0.0f;
        for (int s = 0; s < n; ++s)
        {
            const float l = t.L[(size_t) s] * gl, r = t.R[(size_t) s] * gr;
            L[s] += l; R[s] += r;
            sendL[(size_t) s] += l * send; sendR[(size_t) s] += r * send;
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        }
        t.meter = std::max (peak, t.meter * 0.85f);
        controls.meter[i].store (t.meter);
    }

    reverb.process (sendL.data(), sendR.data(), wetL.data(), wetR.data(), n);
    const float rv = controls.reverbAmount.load();
    for (int s = 0; s < n; ++s) { L[s] += wetL[(size_t) s] * rv; R[s] += wetR[(size_t) s] * rv; }

    glue.process (L, R, n);
    const float mg = controls.masterGain.load();
    for (int s = 0; s < n; ++s) { L[s] *= mg; R[s] *= mg; }
    limiter.process (L, R, n);

    float pl = 0.0f, pr = 0.0f;
    for (int s = 0; s < n; ++s) { pl = std::max (pl, std::abs (L[s])); pr = std::max (pr, std::abs (R[s])); }
    masterMeterL = std::max (pl, masterMeterL * 0.85f);
    masterMeterR = std::max (pr, masterMeterR * 0.85f);
    controls.masterMeterL.store (masterMeterL);
    controls.masterMeterR.store (masterMeterR);

    if (tr)
    {
        tr->position.store ((int64_t) tick);
        tr->playing.store (playing);
    }
}
} // namespace tc
