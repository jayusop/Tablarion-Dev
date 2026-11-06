// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
#include "TrackFX.h"

namespace tc
{
using namespace dsp;

void AmpSim::prepare (float sampleRate, int maxBlock, Type t)
{
    type = t; sr = sampleRate;
    os = std::make_unique<juce::dsp::Oversampling<float>> (1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os->initProcessing ((size_t) std::max (64, maxBlock));
    const float osr = sr * 4.0f;

    switch (type)
    {
        case Clean:
            preHP.set (Biquad::HighPass, 70, 0.7f, 0, sr); preMid.set (Biquad::Peak, 900, 0.7f, 1.5f, sr); preLP.set (Biquad::LowPass, 9000, 0.7f, 0, sr);
            gain1 = 1.8f; gain2 = 1.3f; gain3 = 0; bias = 0.08f; powerSat = 1.2f;
            toneLow.set (Biquad::LowShelf, 140, 0.7f, 2.0f, sr); toneMid.set (Biquad::Peak, 700, 0.8f, -1.0f, sr); toneHigh.set (Biquad::HighShelf, 3200, 0.7f, 2.0f, sr);
            cabLP1.set (Biquad::LowPass, 7500, 0.7f, 0, sr); cabLP2.set (Biquad::LowPass, 9000, 0.6f, 0, sr);
            outGain = 0.9f; break;
        case Jazz:
            preHP.set (Biquad::HighPass, 60, 0.7f, 0, sr); preMid.set (Biquad::Peak, 600, 0.7f, 1.0f, sr); preLP.set (Biquad::LowPass, 6000, 0.7f, 0, sr);
            gain1 = 1.5f; gain2 = 1.2f; gain3 = 0; bias = 0.05f; powerSat = 1.1f;
            toneLow.set (Biquad::LowShelf, 160, 0.7f, 2.5f, sr); toneMid.set (Biquad::Peak, 800, 0.8f, 0.0f, sr); toneHigh.set (Biquad::HighShelf, 2500, 0.7f, -5.0f, sr);
            cabLP1.set (Biquad::LowPass, 5500, 0.7f, 0, sr); cabLP2.set (Biquad::LowPass, 7000, 0.6f, 0, sr);
            outGain = 1.0f; break;
        case Crunch:
            preHP.set (Biquad::HighPass, 95, 0.7f, 0, sr); preMid.set (Biquad::Peak, 820, 0.6f, 6.0f, sr); preLP.set (Biquad::LowPass, 7000, 0.7f, 0, sr);
            gain1 = 9.0f; gain2 = 3.5f; gain3 = 0; bias = 0.15f; powerSat = 1.6f;
            toneLow.set (Biquad::LowShelf, 120, 0.7f, 1.5f, sr); toneMid.set (Biquad::Peak, 650, 0.8f, 0.5f, sr); toneHigh.set (Biquad::HighShelf, 3000, 0.7f, 1.5f, sr);
            cabLP1.set (Biquad::LowPass, 5200, 0.75f, 0, sr); cabLP2.set (Biquad::LowPass, 6500, 0.6f, 0, sr);
            outGain = 0.32f; break;
        case HighGain:
            preHP.set (Biquad::HighPass, 125, 0.75f, 0, sr); preMid.set (Biquad::Peak, 760, 0.6f, 7.0f, sr); preLP.set (Biquad::LowPass, 6500, 0.7f, 0, sr);
            gain1 = 18.0f; gain2 = 6.0f; gain3 = 2.5f; bias = 0.2f; powerSat = 2.0f;
            toneLow.set (Biquad::LowShelf, 110, 0.7f, 4.0f, sr); toneMid.set (Biquad::Peak, 500, 0.7f, -5.5f, sr); toneHigh.set (Biquad::HighShelf, 3500, 0.7f, 2.5f, sr);
            cabLP1.set (Biquad::LowPass, 4800, 0.8f, 0, sr); cabLP2.set (Biquad::LowPass, 6000, 0.6f, 0, sr);
            outGain = 0.2f; break;
        case Bass:
            preHP.set (Biquad::HighPass, 30, 0.7f, 0, sr); preMid.set (Biquad::Peak, 800, 0.7f, 0.0f, sr); preLP.set (Biquad::LowPass, 8000, 0.7f, 0, sr);
            gain1 = 1.6f; gain2 = 1.0f; gain3 = 0; bias = 0.05f; powerSat = 1.2f;
            toneLow.set (Biquad::LowShelf, 90, 0.7f, 2.5f, sr); toneMid.set (Biquad::Peak, 450, 0.8f, -2.0f, sr); toneHigh.set (Biquad::HighShelf, 2500, 0.7f, 1.0f, sr);
            cabLP1.set (Biquad::LowPass, 5000, 0.7f, 0, sr); cabLP2.set (Biquad::LowPass, 8000, 0.6f, 0, sr);
            outGain = 1.0f; break;
    }
    interLP.set (Biquad::LowPass, 7000, 0.6f, 0, osr);
    interHP.set (Biquad::HighPass, type == HighGain ? 30.0f : 15.0f, 0.6f, 0, osr);
    cabHP.set (Biquad::HighPass, type == Bass ? 35.0f : 75.0f, 0.75f, 0, sr);
    cabRes.set (Biquad::Peak, type == Bass ? 80.0f : 115.0f, 1.2f, 3.0f, sr);
    cabMidDip.set (Biquad::Peak, 420, 1.0f, -2.5f, sr);
    cabPresence.set (Biquad::Peak, type == Bass ? 1800.0f : 2400.0f, 1.0f, 2.5f, sr);
    cabNotch.set (Biquad::Peak, 6500, 2.0f, -6.0f, sr);
}

void AmpSim::process (float* mono, int n)
{
    for (int i = 0; i < n; ++i) mono[i] = preLP.process (preMid.process (preHP.process (mono[i])));

    float* chans[1] = { mono };
    juce::dsp::AudioBlock<float> block (chans, 1, (size_t) n);
    auto up = os->processSamplesUp (block);
    float* u = up.getChannelPointer (0);
    const int un = (int) up.getNumSamples();
    const float off1 = std::tanh (bias);
    for (int i = 0; i < un; ++i)
    {
        float x = u[i];
        x = std::tanh (gain1 * x + bias) - off1;                     // stage 1 (asymmetric)
        x = interHP.process (interLP.process (x));
        x = fastTanh (gain2 * x);                                     // stage 2
        if (gain3 > 0.0f) x = std::tanh (gain3 * x - 0.1f) + 0.0997f; // stage 3 (high gain)
        u[i] = x;
    }
    os->processSamplesDown (block);

    for (int i = 0; i < n; ++i)
    {
        float x = toneHigh.process (toneMid.process (toneLow.process (mono[i])));
        x = fastTanh (x * powerSat) / powerSat;                        // power amp
        x = cabHP.process (x); x = cabRes.process (x); x = cabMidDip.process (x); x = cabPresence.process (x);
        x = cabLP2.process (cabLP1.process (x)); x = cabNotch.process (x);
        mono[i] = x * outGain;
    }
}

// ----------------------------------------------------------------------------------------------

void BodyResonator::prepare (float sampleRate, Family f)
{
    // main air / top / back modes of a dreadnought (steel) or classical (nylon) body
    float fr[6] = { 98, 195, 290, 410, 560, 900 };
    float q[6]  = { 12, 14, 10, 8, 7, 5 };
    float g[6]  = { 1.0f, 0.9f, 0.6f, 0.45f, 0.35f, 0.25f };
    mix = 0.45f;
    if (f == Family::NylonGuitar || f == Family::Ukulele) { fr[0] = 105; fr[1] = 205; fr[2] = 310; mix = 0.5f; }
    if (f == Family::Banjo) { fr[0] = 250; fr[1] = 520; fr[2] = 800; fr[3] = 1300; fr[4] = 2100; fr[5] = 3200; mix = 0.4f; }
    if (f == Family::Mandolin) { for (auto& x : fr) x *= 1.6f; }
    if (f == Family::AcousticBass) { for (auto& x : fr) x *= 0.6f; mix = 0.5f; }
    for (int i = 0; i < 6; ++i)
    {
        modesL[i].set (Biquad::BandPass, fr[i], q[i], 0, sampleRate);
        modesR[i].set (Biquad::BandPass, fr[i] * 1.012f, q[i], 0, sampleRate);
        gains[i] = g[i];
    }
    airL.set (Biquad::HighShelf, 9000, 0.7f, 2.5f, sampleRate); airR = airL;
    lowL.set (Biquad::Peak, 220, 0.8f, -1.5f, sampleRate); lowR = lowL;
}

void BodyResonator::process (float* L, float* R, int n)
{
    for (int i = 0; i < n; ++i)
    {
        float bl = 0, br = 0;
        for (int m = 0; m < 6; ++m) { bl += modesL[m].process (L[i]) * gains[m]; br += modesR[m].process (R[i]) * gains[m]; }
        L[i] = airL.process (lowL.process (L[i] + bl * mix * 2.0f));
        R[i] = airR.process (lowR.process (R[i] + br * mix * 2.0f));
    }
}

// ----------------------------------------------------------------------------------------------

void TrackFX::prepare (float sampleRate, int maxBlock, Family f)
{
    family = f; sr = sampleRate;
    mono.assign ((size_t) std::max (64, maxBlock), 0.0f);
    useAmp = useBody = useChorus = useComp = false;
    numEq = 0; width = 0.0f; level = 1.0f;

    auto eq = [&] (Biquad::Type t, float fr, float q, float g)
    {
        if (numEq >= 3) return;
        eqL[numEq].set (t, fr, q, g, sr); eqR[numEq] = eqL[numEq]; ++numEq;
    };

    switch (f)
    {
        case Family::SteelGuitar: case Family::NylonGuitar: case Family::Ukulele: case Family::Mandolin: case Family::Banjo:
            useBody = true; body.prepare (sr, f);
            useComp = true; comp.prepare (sr); comp.set (-20, 2.0f, 15, 150, 2);
            send = 0.22f; level = 1.1f; width = 0.35f; break;
        case Family::CleanElectric: case Family::HarmonicsGuitar:
            useAmp = true; amp.prepare (sr, maxBlock, AmpSim::Clean);
            useChorus = true; chorus.prepare (sr); chorus.set (0.6f, 2.5f, 0.22f);
            send = 0.2f; level = 1.0f; width = 0.4f; break;
        case Family::JazzElectric:
            useAmp = true; amp.prepare (sr, maxBlock, AmpSim::Jazz); send = 0.18f; width = 0.25f; break;
        case Family::MutedElectric:
            useAmp = true; amp.prepare (sr, maxBlock, AmpSim::Clean); send = 0.12f; width = 0.2f; break;
        case Family::OverdriveGuitar:
            useAmp = true; amp.prepare (sr, maxBlock, AmpSim::Crunch); send = 0.14f; width = 0.55f; break;
        case Family::DistortionGuitar:
            useAmp = true; amp.prepare (sr, maxBlock, AmpSim::HighGain); send = 0.1f; width = 0.65f; break;
        case Family::FingerBass: case Family::PickBass: case Family::SlapBass: case Family::FretlessBass:
            useAmp = true; amp.prepare (sr, maxBlock, AmpSim::Bass);
            useComp = true; comp.prepare (sr); comp.set (-22, 4.0f, 8, 120, 5);
            send = 0.03f; level = 1.1f; break;
        case Family::AcousticBass:
            useBody = true; body.prepare (sr, f); useComp = true; comp.prepare (sr); comp.set (-22, 3.0f, 10, 150, 3);
            send = 0.08f; break;
        case Family::SynthBass:
            useComp = true; comp.prepare (sr); comp.set (-20, 3.0f, 10, 120, 3); send = 0.03f; break;
        case Family::Drums:
            useComp = true; comp.prepare (sr); comp.set (-16, 3.0f, 8, 110, 4);
            eq (Biquad::Peak, 60, 0.9f, 2.0f); eq (Biquad::Peak, 400, 1.0f, -2.5f); eq (Biquad::HighShelf, 8000, 0.7f, 2.0f);
            send = 0.11f; level = 1.0f; break;
        case Family::Piano:
            useComp = true; comp.prepare (sr); comp.set (-18, 2.0f, 20, 200, 2);
            eq (Biquad::LowShelf, 120, 0.7f, 1.5f); eq (Biquad::HighShelf, 7000, 0.7f, 1.5f);
            send = 0.22f; level = 1.0f; break;
        case Family::EPiano:
            useChorus = true; chorus.prepare (sr); chorus.set (0.8f, 3.0f, 0.3f); send = 0.2f; break;
        case Family::Organ:
            useChorus = true; chorus.prepare (sr); chorus.set (6.2f, 1.2f, 0.45f); send = 0.18f; break;
        case Family::Strings: case Family::Choir: case Family::SynthPad:
            useChorus = true; chorus.prepare (sr); chorus.set (0.35f, 4.0f, 0.3f); send = 0.35f; break;
        case Family::Brass: case Family::Reed: case Family::Pipe:
            send = 0.25f; break;
        case Family::Harp: case Family::Mallets: case Family::Sitar:
            send = 0.3f; break;
        default: send = 0.2f; break;
    }

    if (width > 0.0f) widen.prepare ((int) (sr * 0.03f));
}

void TrackFX::process (float* L, float* R, int n)
{
    if (useAmp)
    {
        for (int i = 0; i < n; ++i) mono[(size_t) i] = (L[i] + R[i]) * 0.5f;
        amp.process (mono.data(), n);
        for (int i = 0; i < n; ++i) { L[i] = mono[(size_t) i]; R[i] = mono[(size_t) i]; }
    }
    if (useBody) body.process (L, R, n);
    if (width > 0.0f)
    {
        // micro-delay decorrelation for a wider stereo image (mono-compatible amount)
        const int d = (int) (0.011f * sr);
        const float amt = width * 0.35f;
        for (int i = 0; i < n; ++i)
        {
            widen.push ((L[i] + R[i]) * 0.5f);
            const float wet = widen.readInt (d) * amt; // added to L, subtracted from R: cancels in mono
            L[i] += wet;
            R[i] -= wet;
        }
    }
    if (useChorus) chorus.process (L, R, n);
    for (int b = 0; b < numEq; ++b)
        for (int i = 0; i < n; ++i) { L[i] = eqL[b].process (L[i]); R[i] = eqR[b].process (R[i]); }
    if (useComp) comp.process (L, R, n);
    if (level != 1.0f) for (int i = 0; i < n; ++i) { L[i] *= level; R[i] *= level; }
}
} // namespace tc
