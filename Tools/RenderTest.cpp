// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Offline renderer: RenderTest <score> <out.wav> [seconds] [soundfont.sf2]
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/IO/GPReader.h"
#include "../Source/Engine/SongEngine.h"
#include "../Source/Engine/SoundFont.h"

static int renderNote (int program, bool drums, float midi, const char* outPath, uint32_t flags)
{
    tc::Track t; t.program = program; t.percussion = drums; t.tuning = { 40, 45, 50, 55, 59, 64 };
    if (program >= 32 && program <= 39) t.tuning = { 28, 33, 38, 43 };
    t.family = tc::classifyTrack (t);
    auto inst = tc::makeModeledInstrument (t);
    const float sr = 48000.0f; inst->prepare (sr);
    tc::TrackFX fx; fx.prepare (sr, 512, t.family);
    tc::NoteEvent e; e.pitch = midi; e.velocity = 0.8f; e.durTicks = tc::PPQ * 4; e.curveTicks = e.durTicks; e.flags = flags;
    e.string = -1;
    inst->noteOn (e, 60.0 / (120.0 * tc::PPQ));
    const int total = (int) (sr * 2.5f);
    juce::AudioBuffer<float> out (2, total); out.clear();
    for (int pos = 0; pos < total; pos += 512) { int n = std::min (512, total - pos); inst->render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n); fx.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n); }
    printf ("%s prog %d midi %.1f -> %s peak %.3f rms %.4f\n", tc::familyName (t.family), program, midi, inst->engineName(), out.getMagnitude (0, total), out.getRMSLevel (0, 0, total));
    juce::File f (outPath); f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (out, 0, total);
    return 0;
}

int main (int argc, char** argv)
{
    if (argc >= 5 && juce::String (argv[1]) == "--note")
        return renderNote (atoi (argv[2]), juce::String (argv[2]) == "drums", (float) atof (argv[3]), argv[4], argc > 5 ? (uint32_t) atoi (argv[5]) : 0u);
    if (argc < 3) { printf ("usage: RenderTest <score> <out.wav> [seconds] [sf2]\n"); return 1; }
    juce::String err;
    auto score = std::shared_ptr<tc::Score> (tc::loadGuitarPro (juce::File (juce::String::fromUTF8 (argv[1])), err));
    if (! score) { printf ("load failed: %s\n", err.toRawUTF8()); return 1; }
    const double maxSec = argc > 3 ? atof (argv[3]) : 30.0;
    std::shared_ptr<tc::SoundFont> sf;
    if (argc > 4) { sf = tc::SoundFont::load (juce::File (argv[4]), err); if (! sf) printf ("sf2: %s\n", err.toRawUTF8()); }

    tc::CompileOptions co;
    auto song = std::shared_ptr<tc::CompiledSong> (tc::compileScore (*score, co));
    printf ("%s - %s | events %d | length %.1fs\n", score->artist.c_str(), score->title.c_str(), (int) song->events.size(), song->lengthSeconds);

    const float sr = 48000.0f; const int block = 512;
    auto controls = std::make_unique<tc::EngineControls>();
    for (size_t i = 0; i < score->tracks.size() && i < tc::kMaxTracks; ++i) { controls->volume[i] = score->tracks[i].volume; controls->pan[i] = score->tracks[i].pan; }
    tc::SongEngine engine (score, song, sf, sf ? tc::SoundMode::Hybrid : tc::SoundMode::Modeled, *controls, sr, block);
    for (int i = 0; i < engine.numTracks(); ++i) printf ("  track %d %-24s %-18s -> %s\n", i, score->tracks[i].name.c_str(), tc::familyName (score->tracks[i].family), engine.trackEngineName (i).toRawUTF8());
    engine.play();

    const int total = (int) (std::min (maxSec, song->lengthSeconds + 2.0) * sr);
    juce::AudioBuffer<float> out (2, total);
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    float trackPeak[64] = {};
    for (int pos = 0; pos < total; pos += block)
    {
        int n = std::min (block, total - pos);
        engine.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n, nullptr, nullptr);
        for (int i = 0; i < engine.numTracks(); ++i) trackPeak[i] = std::max (trackPeak[i], controls->meter[i].load());
    }
    auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    bool nan = false; for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) if (! std::isfinite (out.getSample (c, i))) nan = true;
    printf ("rendered %.1fs in %.0f ms (%.1fx realtime) peak %.3f rms %.4f nan %d\n", total / sr, ms, total / sr * 1000.0 / ms, out.getMagnitude (0, total), out.getRMSLevel (0, 0, total), (int) nan);
    for (int i = 0; i < engine.numTracks(); ++i) printf ("  track %d peak %.3f\n", i, trackPeak[i]);

    if (std::getenv ("TC_SOLO"))
    {
        for (int solo = 0; solo < engine.numTracks(); ++solo)
        {
            for (int i = 0; i < engine.numTracks(); ++i) controls->solo[i] = (i == solo);
            tc::SongEngine e2 (score, song, sf, sf ? tc::SoundMode::Hybrid : tc::SoundMode::Modeled, *controls, sr, block);
            e2.play();
            juce::AudioBuffer<float> b (2, total);
            for (int pos = 0; pos < total; pos += block) e2.process (b.getWritePointer (0, pos), b.getWritePointer (1, pos), std::min (block, total - pos), nullptr, nullptr);
            printf ("  solo %d %-20s rms %.4f (%.1f dB) peak %.3f\n", solo, tc::familyName (score->tracks[solo].family), b.getRMSLevel (0, 0, total), 20 * log10 (b.getRMSLevel (0, 0, total) + 1e-9), b.getMagnitude (0, total));
        }
    }
    juce::File f (juce::String::fromUTF8 (argv[2]));
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (out, 0, total);
    return 0;
}
