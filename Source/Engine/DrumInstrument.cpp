// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — synthesised General MIDI drum kit
// Every GM percussion key (27-87) has a recipe built from: a pitch-swept tone, a second tone,
// filtered noise, an inharmonic "metal" bank (cymbals/hats), and a transient click.
// Each hit gets slight random variation (like multi-sampled round robins); hi-hats choke each other.
#include "Instrument.h"

namespace tc
{
namespace
{
using namespace dsp;

struct Recipe
{
    float toneHz = 0, toneEndHz = 0, sweep = 0.03f, toneDecay = 0.2f, toneAmp = 0;
    float tone2Hz = 0, tone2Decay = 0.1f, tone2Amp = 0;
    float noiseAmp = 0, noiseDecay = 0.1f, noiseHP = 20, noiseLP = 18000, noiseBP = 0, noiseQ = 1;
    float metalAmp = 0, metalDecay = 0.1f, metalHP = 6000, metalScale = 1.0f;
    float clickAmp = 0;
    int   bursts = 1;
    float pan = 0;
    int   choke = -1;
    float level = 1.0f;
};

Recipe recipeFor (int k)
{
    Recipe r;
    auto tom = [&] (float hz, float pan)
    {
        r.toneHz = hz * 1.7f; r.toneEndHz = hz; r.sweep = 0.045f; r.toneDecay = 0.5f * std::sqrt (100.0f / hz) + 0.15f; r.toneAmp = 0.9f;
        r.tone2Hz = hz * 1.5f; r.tone2Decay = 0.12f; r.tone2Amp = 0.15f;
        r.noiseAmp = 0.12f; r.noiseLP = 5000; r.noiseDecay = 0.05f; r.clickAmp = 0.12f; r.pan = pan;
    };
    switch (k)
    {
        case 35: case 36:
            r.toneHz = 170; r.toneEndHz = 47; r.sweep = 0.032f; r.toneDecay = 0.42f; r.toneAmp = 1.0f;
            r.noiseAmp = 0.07f; r.noiseLP = 3500; r.noiseDecay = 0.012f; r.clickAmp = 0.45f; r.level = 1.15f; break;
        case 37:
            r.toneHz = 430; r.toneEndHz = 420; r.toneDecay = 0.03f; r.toneAmp = 0.45f;
            r.noiseAmp = 0.6f; r.noiseBP = 1900; r.noiseQ = 2.2f; r.noiseDecay = 0.03f; r.clickAmp = 0.3f; break;
        case 38: case 40:
            r.toneHz = 205; r.toneEndHz = 180; r.sweep = 0.02f; r.toneDecay = 0.12f; r.toneAmp = 0.55f;
            r.tone2Hz = 335; r.tone2Decay = 0.07f; r.tone2Amp = 0.3f;
            r.noiseAmp = 0.8f; r.noiseHP = 1400; r.noiseLP = k == 40 ? 11000.0f : 8500.0f; r.noiseDecay = k == 40 ? 0.22f : 0.19f; r.clickAmp = 0.2f; break;
        case 39:
            r.noiseAmp = 0.9f; r.noiseBP = 1250; r.noiseQ = 1.4f; r.noiseDecay = 0.12f; r.bursts = 3; break;
        case 41: tom (82, 0.4f); break;
        case 43: tom (98, 0.32f); break;
        case 45: tom (112, 0.15f); break;
        case 47: tom (132, 0.0f); break;
        case 48: tom (150, -0.15f); break;
        case 50: tom (178, -0.3f); break;
        case 42:
            r.metalAmp = 0.55f; r.metalDecay = 0.045f; r.metalHP = 7500; r.noiseAmp = 0.25f; r.noiseHP = 8000; r.noiseDecay = 0.035f;
            r.pan = -0.3f; r.choke = 1; r.level = 0.8f; break;
        case 44:
            r.metalAmp = 0.4f; r.metalDecay = 0.055f; r.metalHP = 7000; r.noiseAmp = 0.15f; r.noiseHP = 7000; r.noiseDecay = 0.04f;
            r.pan = -0.3f; r.choke = 1; r.level = 0.7f; break;
        case 46:
            r.metalAmp = 0.55f; r.metalDecay = 0.55f; r.metalHP = 6500; r.noiseAmp = 0.25f; r.noiseHP = 7500; r.noiseDecay = 0.45f;
            r.pan = -0.3f; r.choke = 1; r.level = 0.75f; break;
        case 49: case 57:
            r.metalAmp = 0.45f; r.metalDecay = 1.7f; r.metalHP = 4200; r.metalScale = k == 49 ? 1.0f : 1.12f;
            r.noiseAmp = 0.4f; r.noiseHP = 3800; r.noiseDecay = 1.4f; r.pan = k == 49 ? -0.45f : 0.45f; r.level = 0.75f; break;
        case 51: case 59:
            r.metalAmp = 0.22f; r.metalDecay = 1.5f; r.metalHP = 3200; r.metalScale = 1.35f;
            r.noiseAmp = 0.22f; r.noiseHP = 6000; r.noiseDecay = 0.3f; r.tone2Hz = 3150; r.tone2Decay = 0.6f; r.tone2Amp = 0.05f;
            r.pan = 0.45f; r.level = 0.7f; break;
        case 53:
            r.toneHz = 770; r.toneEndHz = 770; r.toneDecay = 1.1f; r.toneAmp = 0.3f; r.tone2Hz = 1150; r.tone2Decay = 0.8f; r.tone2Amp = 0.22f;
            r.metalAmp = 0.2f; r.metalDecay = 1.0f; r.metalHP = 3000; r.metalScale = 1.35f; r.pan = 0.45f; r.level = 0.7f; break;
        case 52:
            r.metalAmp = 0.55f; r.metalDecay = 1.2f; r.metalHP = 2500; r.metalScale = 0.85f;
            r.noiseAmp = 0.4f; r.noiseBP = 3200; r.noiseQ = 0.8f; r.noiseDecay = 0.9f; r.pan = 0.55f; r.level = 0.7f; break;
        case 55:
            r.metalAmp = 0.45f; r.metalDecay = 0.65f; r.metalHP = 5500; r.metalScale = 1.25f;
            r.noiseAmp = 0.3f; r.noiseHP = 6000; r.noiseDecay = 0.5f; r.pan = -0.2f; r.level = 0.7f; break;
        case 54:
            r.metalAmp = 0.4f; r.metalDecay = 0.25f; r.metalHP = 7500; r.metalScale = 1.6f; r.noiseAmp = 0.3f; r.noiseHP = 6500;
            r.noiseDecay = 0.15f; r.bursts = 2; r.pan = 0.3f; r.level = 0.6f; break;
        case 56:
            r.toneHz = 545; r.toneEndHz = 545; r.toneDecay = 0.28f; r.toneAmp = 0.4f; r.tone2Hz = 815; r.tone2Decay = 0.22f; r.tone2Amp = 0.35f;
            r.noiseAmp = 0.1f; r.noiseBP = 2500; r.noiseDecay = 0.02f; r.pan = 0.25f; r.level = 0.6f; break;
        case 58: r.noiseAmp = 0.5f; r.noiseBP = 3000; r.noiseQ = 3; r.noiseDecay = 0.7f; r.level = 0.5f; break;
        case 60: case 61:
            r.toneHz = k == 60 ? 420.0f : 310.0f; r.toneEndHz = r.toneHz * 0.92f; r.sweep = 0.01f; r.toneDecay = 0.13f; r.toneAmp = 0.7f;
            r.noiseAmp = 0.1f; r.noiseBP = 2500; r.noiseDecay = 0.02f; r.pan = k == 60 ? -0.25f : -0.1f; break;
        case 62: case 63: case 64:
            r.toneHz = k == 64 ? 240.0f : 335.0f; r.toneEndHz = r.toneHz * 0.95f; r.sweep = 0.01f; r.toneDecay = k == 62 ? 0.07f : 0.27f; r.toneAmp = 0.75f;
            r.noiseAmp = 0.12f; r.noiseBP = 2000; r.noiseDecay = 0.02f; r.pan = k == 64 ? 0.3f : 0.15f; break;
        case 65: case 66:
            r.toneHz = k == 65 ? 470.0f : 340.0f; r.toneEndHz = r.toneHz; r.toneDecay = 0.25f; r.toneAmp = 0.6f;
            r.metalAmp = 0.12f; r.metalDecay = 0.2f; r.metalHP = 3000; r.noiseAmp = 0.3f; r.noiseBP = 3500; r.noiseDecay = 0.12f; break;
        case 67: case 68:
            r.toneHz = k == 67 ? 905.0f : 640.0f; r.toneEndHz = r.toneHz; r.toneDecay = 0.45f; r.toneAmp = 0.4f;
            r.tone2Hz = r.toneHz * 2.71f; r.tone2Decay = 0.2f; r.tone2Amp = 0.15f; r.level = 0.6f; break;
        case 69: r.noiseAmp = 0.5f; r.noiseHP = 6000; r.noiseDecay = 0.07f; r.level = 0.5f; break;
        case 70: case 82: r.noiseAmp = 0.5f; r.noiseHP = 7000; r.noiseDecay = 0.045f; r.level = 0.45f; break;
        case 71: case 72:
            r.toneHz = k == 71 ? 2450.0f : 2050.0f; r.toneEndHz = r.toneHz; r.toneDecay = k == 71 ? 0.2f : 0.6f; r.toneAmp = 0.25f; r.level = 0.4f; break;
        case 73: case 74: r.noiseAmp = 0.5f; r.noiseBP = 2600; r.noiseQ = 2; r.noiseDecay = k == 73 ? 0.05f : 0.2f; r.bursts = 4; r.level = 0.5f; break;
        case 75: r.toneHz = 2500; r.toneEndHz = 2500; r.toneDecay = 0.05f; r.toneAmp = 0.5f; r.level = 0.55f; break;
        case 76: case 77:
            r.toneHz = k == 76 ? 1650.0f : 1150.0f; r.toneEndHz = r.toneHz; r.toneDecay = 0.07f; r.toneAmp = 0.55f;
            r.noiseAmp = 0.1f; r.noiseBP = 3000; r.noiseDecay = 0.01f; r.level = 0.6f; break;
        case 78: case 79:
            r.toneHz = k == 78 ? 750.0f : 600.0f; r.toneEndHz = r.toneHz * 0.6f; r.sweep = 0.08f; r.toneDecay = 0.2f; r.toneAmp = 0.4f; break;
        case 80: case 81:
            r.toneHz = 4200; r.toneEndHz = 4200; r.toneDecay = k == 80 ? 0.12f : 1.3f; r.toneAmp = 0.18f;
            r.tone2Hz = 9100; r.tone2Decay = k == 80 ? 0.08f : 0.9f; r.tone2Amp = 0.1f; r.pan = 0.4f; r.level = 0.5f; break;
        case 83: r.metalAmp = 0.35f; r.metalDecay = 0.35f; r.metalHP = 6000; r.metalScale = 1.9f; r.level = 0.5f; break;
        case 84: r.metalAmp = 0.3f; r.metalDecay = 1.2f; r.metalHP = 5000; r.metalScale = 2.3f; r.level = 0.4f; break;
        case 85: r.noiseAmp = 0.5f; r.noiseBP = 2200; r.noiseQ = 2; r.noiseDecay = 0.025f; r.bursts = 2; r.level = 0.5f; break;
        case 86: case 87:
            r.toneHz = 120; r.toneEndHz = 90; r.toneDecay = k == 86 ? 0.15f : 0.5f; r.toneAmp = 0.8f; r.noiseAmp = 0.1f; r.noiseLP = 2000; break;
        case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34:
            r.noiseAmp = 0.4f; r.noiseBP = 2500; r.noiseDecay = 0.04f; r.clickAmp = 0.3f; r.level = 0.5f; break;
        default:
            r.toneHz = 200; r.toneEndHz = 180; r.toneDecay = 0.15f; r.toneAmp = 0.5f; r.noiseAmp = 0.2f; r.noiseDecay = 0.08f; break;
    }
    return r;
}

class DrumVoice
{
public:
    bool active = false;
    int key = -1, choke = -1;
    uint64_t age = 0;

    void prepare (float sampleRate, uint32_t seed) { sr = sampleRate; noise = Noise (seed); }

    void start (int k, float vel, uint64_t stamp, float variation, float brightness)
    {
        key = k; age = stamp; active = true; t = 0;
        r = recipeFor (k);
        choke = r.choke;
        const float amp = (0.18f + 0.82f * std::pow (vel, 1.25f)) * r.level;
        const float vary = 1.0f + 0.02f * variation;
        const float decVary = 1.0f + 0.08f * variation;

        tonePh = tone2Ph = 0.0f;
        toneA = r.toneAmp * amp; tone2A = r.tone2Amp * amp;
        toneMul = mulFor (r.toneDecay * decVary);
        tone2Mul = mulFor (r.tone2Decay * decVary);
        sweepEnv = 1.0f; sweepMul = r.sweep > 0 ? std::exp (-1.0f / (r.sweep * sr)) : 0.0f;
        f0 = r.toneHz * vary * (0.96f + 0.08f * vel); f1 = r.toneEndHz * vary;
        f2 = r.tone2Hz * vary;

        noiseA = r.noiseAmp * amp; noiseMul = mulFor (r.noiseDecay * decVary);
        nhp.setCutoff (r.noiseHP, sr);
        nlp.setCutoff (std::min (r.noiseLP * (0.55f + 0.45f * brightness * vel + 0.2f), sr * 0.45f), sr);
        useBP = r.noiseBP > 0;
        if (useBP) bp.set (Biquad::BandPass, r.noiseBP * vary, r.noiseQ, 0, sr);

        metalA = r.metalAmp * amp; metalMul = mulFor (r.metalDecay * decVary);
        mhp1.set (Biquad::HighPass, r.metalHP, 0.7f, 0, sr);
        mhp2.set (Biquad::HighPass, r.metalHP * 1.05f, 0.7f, 0, sr);
        static const float mf[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        for (int i = 0; i < 6; ++i) { mInc[i] = mf[i] * r.metalScale * vary * 1.47f / sr; mPh[i] = 0.17f * (float) i; }

        clickA = r.clickAmp * amp;
        clickHP.setCutoff (2500.0f, sr);
        burst = 0; burstGap = (int) (0.011f * sr); burstCount = 0;

        const float a = (std::clamp (r.pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        panL = std::cos (a); panR = std::sin (a);
        peak = 1.0f;
    }

    void chokeNow() { metalMul = std::min (metalMul, mulFor (0.03f)); noiseMul = std::min (noiseMul, mulFor (0.03f)); }
    void kill() { active = false; }

    void render (float* L, float* R, int n)
    {
        if (! active) return;
        for (int s = 0; s < n; ++s)
        {
            float y = 0.0f;
            if (toneA > 1.0e-6f)
            {
                const float f = f1 + (f0 - f1) * sweepEnv;
                sweepEnv *= sweepMul;
                tonePh += kTwoPi * f / sr;
                if (tonePh > kTwoPi) tonePh -= kTwoPi;
                y += std::sin (tonePh) * toneA;
                toneA *= toneMul;
            }
            if (tone2A > 1.0e-6f)
            {
                tone2Ph += kTwoPi * f2 / sr;
                if (tone2Ph > kTwoPi) tone2Ph -= kTwoPi;
                y += std::sin (tone2Ph) * tone2A;
                tone2A *= tone2Mul;
            }
            if (noiseA > 1.0e-6f)
            {
                float nz = noise.next();
                nz = nhp.process (nz);
                nz = nlp.process (nz);
                if (useBP) nz = bp.process (nz) * 2.5f;
                float g = noiseA;
                if (r.bursts > 1 && burstCount < r.bursts - 1)
                {
                    // clap: several quick re-attacks
                    const float local = (float) (burst % burstGap) / (float) burstGap;
                    g *= (1.0f - local) * 1.4f;
                    if (++burst % burstGap == 0) ++burstCount;
                }
                y += nz * g;
                noiseA *= noiseMul;
            }
            if (metalA > 1.0e-6f)
            {
                float m = 0.0f;
                for (int i = 0; i < 6; ++i)
                {
                    mPh[i] += mInc[i];
                    if (mPh[i] >= 1.0f) mPh[i] -= 1.0f;
                    m += mPh[i] < 0.5f ? 1.0f : -1.0f;
                }
                m = mhp2.process (mhp1.process (m * 0.25f + noise.next() * 0.15f));
                y += m * metalA;
                metalA *= metalMul;
            }
            if (clickA > 1.0e-5f)
            {
                y += clickHP.process (noise.next()) * clickA;
                clickA *= 0.92f;
            }
            y = fastTanh (y * 1.1f) * 0.9f;
            peak = std::max (std::abs (y), peak * 0.999f);
            L[s] += y * panL;
            R[s] += y * panR;
        }
        t += (float) n / sr;
        if (t > 0.02f && peak < 1.0e-5f) active = false;
        if (t > 8.0f) active = false;
    }

private:
    float sr = 48000.0f, t = 0.0f;
    Recipe r;
    Noise noise;
    float tonePh = 0, tone2Ph = 0, toneA = 0, tone2A = 0, toneMul = 0, tone2Mul = 0;
    float sweepEnv = 0, sweepMul = 0, f0 = 0, f1 = 0, f2 = 0;
    float noiseA = 0, noiseMul = 0;
    OnePoleHP nhp; OnePoleLP nlp; Biquad bp; bool useBP = false;
    float metalA = 0, metalMul = 0, mPh[6] {}, mInc[6] {};
    Biquad mhp1, mhp2;
    float clickA = 0; OnePoleHP clickHP;
    int burst = 0, burstGap = 1, burstCount = 0;
    float panL = 0.7f, panR = 0.7f, peak = 1.0f;

    float mulFor (float t60) const { return std::exp (-6.9f / (std::max (0.002f, t60) * sr)); }
};

class DrumInstrument final : public Instrument
{
public:
    DrumInstrument() { voices.resize (40); }
    const char* engineName() const override { return "Synth Drum Kit"; }

    void prepare (float sampleRate) override
    {
        for (size_t i = 0; i < voices.size(); ++i) voices[i].prepare (sampleRate, 0xD00Du + (uint32_t) i * 2654435761u);
    }

    void noteOn (const NoteEvent& e, double) override
    {
        const int k = std::clamp ((int) std::lround (e.pitch), 0, 127);
        const auto rc = recipeFor (k);
        if (rc.choke >= 0)
            for (auto& v : voices) if (v.active && v.choke == rc.choke) v.chokeNow();
        DrumVoice* v = nullptr;
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; }
        v->start (k, e.velocity, ++stamp, rng.next(), 1.0f);
    }

    void noteOff (int) override {}
    void allNotesOff (bool immediate) override { if (immediate) for (auto& v : voices) v.kill(); }
    void render (float* L, float* R, int n) override { for (auto& v : voices) if (v.active) v.render (L, R, n); }

private:
    std::vector<DrumVoice> voices;
    Noise rng { 0xBEEFu };
    uint64_t stamp = 0;
};
} // namespace

std::unique_ptr<Instrument> makeDrumInstrument() { return std::make_unique<DrumInstrument>(); }
} // namespace tc
