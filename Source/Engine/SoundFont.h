// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — SoundFont 2 bank
#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <string>
#include <vector>

namespace tc
{
class SoundFont
{
public:
    static std::shared_ptr<SoundFont> load (const juce::File& file, juce::String& error);

    enum { kNumGens = 61 };

    struct Zone
    {
        int16_t gen[kNumGens] {};
        uint64_t setMask = 0;
        int keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
        int link = -1; // instrument (preset zones) or sample (instrument zones)
        bool isSet (int g) const { return (setMask >> g) & 1u; }
    };

    struct Sample
    {
        uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0, rate = 44100;
        int root = 60, correction = 0;
        uint16_t type = 1;
    };

    struct Instrument { std::string name; Zone global; bool hasGlobal = false; std::vector<Zone> zones; };
    struct Preset { std::string name; int bank = 0, program = 0; Zone global; bool hasGlobal = false; std::vector<Zone> zones; };

    /** Fully resolved parameters for one sample voice. */
    struct VoiceParams
    {
        const int16_t* data = nullptr;
        uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0;
        int loopMode = 0;
        double sampleRate = 44100.0;
        float rootKey = 60.0f, tuneCents = 0.0f, scaleTuning = 100.0f;
        float attenuationDb = 0.0f, pan = 0.0f;
        float filterFc = 20000.0f, filterQ = 0.707f;
        float delayVol = 0, attackVol = 0.001f, holdVol = 0, decayVol = 0.001f, sustainVol = 1.0f, releaseVol = 0.01f;
        float delayMod = 0, attackMod = 0.001f, holdMod = 0, decayMod = 0.001f, sustainMod = 1.0f, releaseMod = 0.01f;
        float modEnvToPitch = 0, modEnvToFc = 0;
        float vibLfoDelay = 0, vibLfoFreq = 8.176f, vibLfoToPitch = 0;
        float modLfoDelay = 0, modLfoFreq = 8.176f, modLfoToPitch = 0, modLfoToFc = 0, modLfoToVolDb = 0;
        int exclusiveClass = 0;
        int fixedKey = -1, fixedVel = -1;
        float reverbSend = 0, chorusSend = 0;
    };

    /** Collects the voices to start for a note. Returns the number written. */
    int resolve (int bank, int program, int key, int vel, VoiceParams* out, int maxOut) const;
    bool hasPreset (int bank, int program) const;
    const Preset* findPreset (int bank, int program) const;

    std::string name;
    juce::File file;
    std::vector<int16_t> sampleData;
    std::vector<Sample> samples;
    std::vector<Instrument> instruments;
    std::vector<Preset> presets;
};
} // namespace tc
