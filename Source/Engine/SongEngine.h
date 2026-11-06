// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — real-time song engine: transport, event scheduling, instruments, mixer, master bus
#pragma once

#include <atomic>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Events.h"
#include "Instrument.h"
#include "TrackFX.h"

namespace tc
{
class SoundFont;
constexpr int kMaxTracks = 64;

enum class SoundMode : int { Modeled = 0, SoundFont = 1, Hybrid = 2 };
enum class TrackEngine : int { Auto = 0, Modeled = 1, SoundFont = 2 };

/** Shared, lock-free control surface between UI/host thread and the audio engine. */
struct EngineControls
{
    std::atomic<float> volume[kMaxTracks];
    std::atomic<float> pan[kMaxTracks];
    std::atomic<bool>  mute[kMaxTracks];
    std::atomic<bool>  solo[kMaxTracks];
    std::atomic<float> meter[kMaxTracks];
    std::atomic<int>   trackEngine[kMaxTracks];

    std::atomic<float> masterGain { 0.85f };
    std::atomic<float> reverbAmount { 1.0f };
    std::atomic<float> speed { 1.0f };
    std::atomic<bool>  loop { false };
    std::atomic<int>   liveTrack { 0 };
    std::atomic<float> masterMeterL { 0.0f }, masterMeterR { 0.0f };

    EngineControls() { resetTracks(); }
    void resetTracks()
    {
        for (int i = 0; i < kMaxTracks; ++i)
        {
            volume[i] = 0.8f; pan[i] = 0.5f; mute[i] = false; solo[i] = false; meter[i] = 0.0f; trackEngine[i] = 0;
        }
    }
    void copyFrom (const EngineControls& o)
    {
        for (int i = 0; i < kMaxTracks; ++i)
        {
            volume[i] = o.volume[i].load(); pan[i] = o.pan[i].load(); mute[i] = o.mute[i].load();
            solo[i] = o.solo[i].load(); trackEngine[i] = o.trackEngine[i].load();
        }
        masterGain = o.masterGain.load(); reverbAmount = o.reverbAmount.load(); speed = o.speed.load();
    }
};

/** Transport requests posted by the UI, consumed at the start of the next audio block. */
struct TransportRequests
{
    std::atomic<int>     command { 0 };    // 0 none, 1 play, 2 stop, 3 toggle
    std::atomic<int64_t> seekTick { -1 };
    std::atomic<int64_t> position { 0 };   // reported by the engine
    std::atomic<bool>    playing { false };
};

class SongEngine
{
public:
    SongEngine (std::shared_ptr<const Score> score, std::shared_ptr<const CompiledSong> song,
                std::shared_ptr<const SoundFont> sf, SoundMode mode, EngineControls& controls,
                float sampleRate, int maxBlock);

    /** Renders n samples (replacing L/R). Live MIDI from `midi` goes to controls.liveTrack. */
    void process (float* L, float* R, int n, const juce::MidiBuffer* midi, TransportRequests* transport);

    // Offline / direct control
    void play();
    void stop (bool hard);
    void seek (int64_t tick);
    bool isPlaying() const { return playing; }
    int64_t position() const { return (int64_t) tick; }
    int64_t lengthTicks() const { return song ? song->lengthTicks : 0; }

    /** Host sync: follow the host's musical position (in quarter notes) and tempo. */
    void followHost (bool hostPlaying, double ppqPosition, double hostBpm);

    int numTracks() const { return (int) tracks.size(); }
    juce::String trackEngineName (int i) const;
    std::shared_ptr<const Score> getScore() const { return score; }
    std::shared_ptr<const CompiledSong> getSong() const { return song; }

private:
    struct TrackSlot
    {
        std::unique_ptr<Instrument> inst;
        TrackFX fx;
        std::vector<float> L, R;
        float meter = 0.0f;
        juce::String engine;
    };

    std::shared_ptr<const Score> score;
    std::shared_ptr<const CompiledSong> song;
    std::shared_ptr<const SoundFont> soundFont;
    EngineControls& controls;
    float sr;
    int maxBlock;

    std::vector<TrackSlot> tracks;
    size_t nextEvent = 0;
    double tick = 0.0;
    bool playing = false;
    double hostSpeed = 0.0; // > 0 when following the host tempo
    bool hostWasPlaying = false;

    std::vector<float> sendL, sendR, wetL, wetR;
    dsp::FDNReverb reverb;
    dsp::Compressor glue;
    dsp::Limiter limiter;
    float masterMeterL = 0.0f, masterMeterR = 0.0f;

    void dispatchUntil (double endTick, double secPerTick);
    void handleMidi (const juce::MidiMessage& m);
};
} // namespace tc
