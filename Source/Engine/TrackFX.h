// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — per-track effect chains (one chain per instrument family)
#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../DSP/DSP.h"
#include "../Model/Score.h"

namespace tc
{
/** Guitar/bass amplifier + cabinet with 4x oversampled multi-stage tube-style saturation. */
class AmpSim
{
public:
    enum Type { Clean, Jazz, Crunch, HighGain, Bass };
    void prepare (float sampleRate, int maxBlock, Type t);
    void process (float* mono, int n);

private:
    Type type = Clean;
    float sr = 48000.0f;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    dsp::Biquad preHP, preMid, preLP, interLP, interHP;
    dsp::Biquad toneLow, toneMid, toneHigh;
    dsp::Biquad cabHP, cabRes, cabMidDip, cabPresence, cabLP1, cabLP2, cabNotch;
    float gain1 = 1, gain2 = 1, gain3 = 0, bias = 0.1f, outGain = 1;
    float powerSat = 1.0f;
};

/** Acoustic guitar body: parallel modal resonators. */
class BodyResonator
{
public:
    void prepare (float sampleRate, Family f);
    void process (float* L, float* R, int n);
private:
    dsp::Biquad modesL[6], modesR[6];
    float gains[6] {};
    float mix = 0.4f;
    dsp::Biquad airL, airR, lowL, lowR;
};

class TrackFX
{
public:
    void prepare (float sampleRate, int maxBlock, Family f);
    void process (float* L, float* R, int n);
    float reverbSend() const { return send; }

private:
    Family family = Family::Other;
    float sr = 48000.0f;
    bool useAmp = false, useBody = false, useChorus = false, useComp = false;
    AmpSim amp;
    BodyResonator body;
    dsp::Chorus chorus;
    dsp::Compressor comp;
    dsp::Biquad eqL[3], eqR[3];
    int numEq = 0;
    std::vector<float> mono;
    float send = 0.15f, level = 1.0f, width = 0.0f;
    dsp::DelayLine widen;
};
} // namespace tc
