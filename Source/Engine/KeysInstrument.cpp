// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — keyboard & mallet instruments
//   Piano   : modal synthesis, inharmonic partials (f_n = n f0 sqrt(1 + B n^2)), hammer-position spectrum,
//             double-string beating / two-stage decay, hammer noise, dampers.
//   EPiano  : two-operator FM tine model with velocity "bark".
//   Organ   : nine-drawbar additive tonewheel with key click.
//   Mallets : modal bars (glockenspiel, vibraphone, marimba, xylophone, bells).
#include "Instrument.h"

namespace tc
{
namespace
{
using namespace dsp;

enum class KeysMode { Piano, EPiano, Organ, Mallets };

struct Osc
{
    float c = 1, s = 0, cw = 1, sw = 0, amp = 0, decay = 1, relDecay = 1;
    void setFreq (float hz, float sr) { const float w = kTwoPi * hz / sr; cw = std::cos (w); sw = std::sin (w); }
    float tick()
    {
        const float nc = c * cw - s * sw;
        s = c * sw + s * cw;
        c = nc;
        const float y = s * amp;
        amp *= decay;
        return y;
    }
    void normalise() { const float r = 1.0f / std::sqrt (c * c + s * s); c *= r; s *= r; }
};

struct MalletSpec { int n; float ratio[6]; float amp[6]; float t60[6]; float tremolo; };

MalletSpec malletFor (int program)
{
    switch (program)
    {
        case 8:  return { 4, { 1, 2.76f, 5.4f, 8.93f }, { 1, 0.45f, 0.25f, 0.1f }, { 3.0f, 1.2f, 0.6f, 0.3f }, 0 };
        case 9:  return { 4, { 1, 2.76f, 5.4f, 8.93f }, { 1, 0.5f, 0.35f, 0.15f }, { 4.0f, 1.5f, 0.7f, 0.35f }, 0 };
        case 10: return { 4, { 1, 2.0f, 3.9f, 5.3f }, { 1, 0.4f, 0.25f, 0.1f }, { 2.0f, 1.0f, 0.5f, 0.3f }, 0 };
        case 11: return { 3, { 1, 3.98f, 9.8f }, { 1, 0.35f, 0.12f }, { 4.5f, 1.4f, 0.4f }, 5.2f };
        case 12: return { 3, { 1, 3.99f, 10.6f }, { 1, 0.3f, 0.1f }, { 1.3f, 0.35f, 0.1f }, 0 };
        case 13: return { 4, { 1, 3.0f, 6.1f, 9.3f }, { 1, 0.45f, 0.2f, 0.1f }, { 0.7f, 0.25f, 0.1f, 0.06f }, 0 };
        case 14: return { 6, { 0.5f, 1, 1.19f, 1.56f, 2.0f, 2.66f }, { 0.4f, 1, 0.5f, 0.4f, 0.3f, 0.2f }, { 6, 5, 4, 3.5f, 3, 2 }, 0 };
        case 15: return { 6, { 1, 2, 3, 4, 5, 6 }, { 1, 0.6f, 0.4f, 0.3f, 0.2f, 0.12f }, { 2.5f, 1.8f, 1.2f, 0.9f, 0.6f, 0.4f }, 0 };
        case 108: return { 3, { 1, 5.4f, 11.0f }, { 1, 0.3f, 0.1f }, { 1.2f, 0.3f, 0.1f }, 0 };
        case 112: return { 6, { 1, 2.32f, 4.25f, 6.63f, 9.38f, 12.4f }, { 1, 0.6f, 0.4f, 0.25f, 0.15f, 0.1f }, { 3, 2, 1.4f, 1, 0.7f, 0.5f }, 0 };
        case 114: return { 4, { 1, 2.0f, 3.0f, 4.1f }, { 1, 0.5f, 0.3f, 0.2f }, { 1.5f, 0.8f, 0.4f, 0.2f }, 0 };
        default: return { 5, { 1, 2.4f, 3.9f, 5.6f, 8.2f }, { 1, 0.5f, 0.3f, 0.2f, 0.1f }, { 3, 1.5f, 1, 0.6f, 0.4f }, 0 };
    }
}

class KeysVoice
{
public:
    static constexpr int kMaxOsc = 48;

    bool active = false;
    int key = -1;
    int liveKey = -1;
    uint64_t age = 0;

    void prepare (float sampleRate, uint32_t seed) { sr = sampleRate; noise = Noise (seed); }

    void start (KeysMode m, int program, const NoteEvent& e, double secPerTick, uint64_t stamp)
    {
        mode = m; age = stamp;
        key = (int) std::lround (e.pitch);
        liveKey = (e.flags & EvLive) ? key : -1;
        vel = e.velocity;
        t = 0.0f; released = false; active = true; env = 1.0f; sampleCount = 0;
        durSec = (float) std::max (0.02, e.durTicks * secPerTick);
        pitch = e.pitch;
        const float f0 = midiToHz (pitch);
        const float pan = std::clamp ((pitch - 64.0f) / 64.0f, -1.0f, 1.0f) * (m == KeysMode::Piano ? 0.45f : 0.2f);
        const float a = (pan + 1.0f) * 0.25f * kPi;
        panL = std::cos (a); panR = std::sin (a);
        n = 0;
        noiseAmp = 0.0f;

        switch (mode)
        {
            case KeysMode::Piano:   setupPiano (f0); break;
            case KeysMode::EPiano:  setupEPiano (f0); break;
            case KeysMode::Organ:   setupOrgan (f0); break;
            case KeysMode::Mallets: setupMallets (f0, program); break;
        }
        attack = 0.0f;
        attackInc = 1.0f / std::max (1.0f, (mode == KeysMode::Organ ? 0.004f : 0.0015f) * sr);
    }

    void release()
    {
        if (released) return;
        released = true;
        float relT60 = 0.2f;
        if (mode == KeysMode::Piano) relT60 = key >= 89 ? 2.0f : 0.16f + 0.25f * std::clamp ((48.0f - (float) key) / 30.0f, 0.0f, 1.0f);
        if (mode == KeysMode::EPiano) relT60 = 0.28f;
        if (mode == KeysMode::Organ) relT60 = 0.035f;
        if (mode == KeysMode::Mallets) relT60 = 100.0f; // mallets ring freely
        const float rd = std::pow (10.0f, -3.0f / (relT60 * sr));
        for (int i = 0; i < n; ++i) osc[i].decay = std::min (osc[i].decay, rd);
        if (mode == KeysMode::EPiano || mode == KeysMode::Organ) relGain = rd;
    }

    void kill() { active = false; }

    void render (float* L, float* R, int frames)
    {
        if (! active) return;
        for (int s = 0; s < frames; ++s)
        {
            float y = 0.0f;
            if (mode == KeysMode::EPiano)
            {
                // FM: modulator at the carrier frequency (tine), index decays -> "bark" on hard hits
                epPh += epInc;
                if (epPh > kTwoPi) epPh -= kTwoPi;
                fmIndex *= fmDecay;
                y = std::sin (epPh + fmIndex * std::sin (epPh)) * epAmp;
                epAmp *= epDecay;
                y += osc[2].tick();
                y *= epEnv;
                if (released) epEnv *= relGain;
            }
            else
            {
                for (int i = 0; i < n; ++i) y += osc[i].tick();
                if (mode == KeysMode::Organ && released) { y *= organRel; organRel *= relGain; }
            }
            if (noiseAmp > 1.0e-5f)
            {
                y += noiseLP.process (noise.next()) * noiseAmp;
                noiseAmp *= noiseDecay;
            }
            if (tremRate > 0.0f)
            {
                tremPh += kTwoPi * tremRate / sr;
                if (tremPh > kTwoPi) tremPh -= kTwoPi;
                y *= 1.0f - 0.35f * (0.5f + 0.5f * std::sin (tremPh));
            }
            if (attack < 1.0f) { attack = std::min (1.0f, attack + attackInc); y *= attack; }
            y *= gain;
            peak = std::max (std::abs (y), peak * 0.9995f);
            L[s] += y * panL;
            R[s] += y * panR;
        }
        sampleCount += frames;
        if ((sampleCount & 1023) < frames) for (int i = 0; i < n; ++i) osc[i].normalise();
        t += (float) frames / sr;
        if (! released && liveKey < 0 && t >= durSec) release();
        if (t > 0.05f && peak < 2.0e-5f) active = false;
        if (mode == KeysMode::Organ && released && organRel < 1.0e-4f) active = false;
        if (mode == KeysMode::EPiano && released && epEnv < 1.0e-4f) active = false;
    }

private:
    float sr = 48000.0f;
    KeysMode mode = KeysMode::Piano;
    Osc osc[kMaxOsc];
    int n = 0;
    float vel = 0.7f, pitch = 60.0f, t = 0.0f, durSec = 1.0f;
    bool released = false;
    float env = 1.0f, gain = 0.2f, panL = 0.7f, panR = 0.7f, peak = 1.0f;
    float attack = 0.0f, attackInc = 1.0f;
    Noise noise;
    OnePoleLP noiseLP;
    float noiseAmp = 0.0f, noiseDecay = 0.99f;
    float fmIndex = 0.0f, fmDecay = 1.0f, epEnv = 1.0f, relGain = 1.0f, organRel = 1.0f;
    float tremRate = 0.0f, tremPh = 0.0f;
    float epPh = 0.0f, epInc = 0.0f, epAmp = 1.0f, epDecay = 1.0f;
    int sampleCount = 0;

    void addOsc (float hz, float amp, float t60)
    {
        if (n >= kMaxOsc || hz >= sr * 0.45f || amp <= 1.0e-5f) return;
        auto& o = osc[n++];
        o.c = 1.0f; o.s = 0.0f;
        o.setFreq (hz, sr);
        o.amp = amp;
        o.decay = std::pow (10.0f, -3.0f / (std::max (0.01f, t60) * sr));
    }

    void setupPiano (float f0)
    {
        const float k = pitch;
        const float B = std::clamp (0.00011f * std::exp2 ((k - 48.0f) / 12.0f * 0.95f), 0.00004f, 0.008f);
        const float strike = 1.0f / 7.4f;
        const float t60base = std::clamp (32.0f * std::exp2 (-(k - 21.0f) / 19.0f), 0.7f, 30.0f);
        const float p = 1.95f - 0.95f * vel;
        const float brightHz = 900.0f + 7000.0f * vel * vel + 2.0f * f0;
        const float maxHz = std::min (12000.0f, sr * 0.45f);
        float sum = 0.0f;
        const float detuneCents = 0.35f + 0.9f * (float) ((key * 7919) % 100) / 100.0f;
        for (int h = 1; h <= 40; ++h)
        {
            const float fh = (float) h * f0 * std::sqrt (1.0f + B * (float) (h * h));
            if (fh > maxHz) break;
            float a = std::pow ((float) h, -p) * (0.18f + std::abs (std::sin (kPi * (float) h * strike)));
            a *= 1.0f / (1.0f + std::pow (fh / brightHz, 2.0f));
            const float t60 = t60base / (1.0f + 0.22f * (float) (h - 1) + fh / 3500.0f);
            if (h <= 10 && n + 2 <= kMaxOsc)
            {
                // two (or three) unison strings: prompt sound + aftersound
                const float det = std::exp2 (detuneCents * (1.0f + 0.1f * (float) h) / 1200.0f);
                addOsc (fh, a * 0.55f, t60 * 0.55f);
                addOsc (fh * det, a * 0.45f, t60 * 1.7f);
            }
            else addOsc (fh, a, t60);
            sum += a;
        }
        gain = (0.10f + 0.9f * std::pow (vel, 1.7f)) * 0.55f / std::max (0.5f, sum);
        // hammer felt thump
        noiseAmp = 0.06f * vel * vel / std::max (0.2f, gain * 4.0f) * gain * 4.0f;
        noiseDecay = std::exp (-1.0f / (0.006f * sr));
        noiseLP.setCutoff (1200.0f + 3000.0f * vel, sr);
        tremRate = 0.0f;
    }

    void setupEPiano (float f0)
    {
        n = 0;
        const float t60 = std::clamp (9.0f * std::exp2 (-(pitch - 40.0f) / 24.0f), 1.2f, 9.0f);
        addOsc (f0, 0.0f, 1.0f);        // (unused slots keep the tine at index 2)
        addOsc (f0, 0.0f, 1.0f);
        n = 2;
        addOsc (f0 * 7.03f, 0.08f * vel, 0.06f); // tine "ping"
        epPh = 0.0f; epInc = kTwoPi * f0 / sr; epAmp = 1.0f;
        epDecay = std::pow (10.0f, -3.0f / (t60 * sr));
        fmIndex = 0.3f + 2.4f * vel * vel;
        fmDecay = std::exp (-1.0f / (0.35f * sr));
        epEnv = 1.0f; relGain = 1.0f;
        gain = 0.32f * (0.25f + 0.75f * vel);
        noiseAmp = 0.0f;
        tremRate = 0.0f;
    }

    void setupOrgan (float f0)
    {
        static const float ratio[9] = { 0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
        static const float bars[9]  = { 8, 6, 8, 5, 0, 3, 0, 0, 3 };
        float sum = 0.0f;
        for (int i = 0; i < 9; ++i)
        {
            if (bars[i] <= 0) continue;
            const float a = std::pow (10.0f, (bars[i] - 8.0f) * 3.0f / 20.0f);
            addOsc (f0 * ratio[i], a, 1000.0f);
            osc[n - 1].s = 0.0f;
            sum += a;
        }
        gain = 0.22f / std::max (1.0f, sum * 0.5f);
        noiseAmp = 0.15f; // key click
        noiseDecay = std::exp (-1.0f / (0.0015f * sr));
        noiseLP.setCutoff (4000.0f, sr);
        organRel = 1.0f; relGain = 1.0f;
        tremRate = 0.0f;
    }

    void setupMallets (float f0, int program)
    {
        auto spec = malletFor (program);
        float sum = 0.0f;
        for (int i = 0; i < spec.n; ++i)
        {
            const float a = spec.amp[i] * (i == 0 ? 1.0f : (0.4f + 0.6f * vel));
            addOsc (f0 * spec.ratio[i], a, spec.t60[i] * std::clamp (std::exp2 (-(pitch - 60.0f) / 30.0f), 0.3f, 2.0f));
            sum += a;
        }
        gain = (0.2f + 0.8f * vel) * 0.35f / std::max (0.5f, sum);
        noiseAmp = 0.05f * vel;
        noiseDecay = std::exp (-1.0f / (0.002f * sr));
        noiseLP.setCutoff (5000.0f, sr);
        tremRate = spec.tremolo;
    }
};

class KeysInstrument final : public Instrument
{
public:
    KeysInstrument (KeysMode m, int prog) : mode (m), program (prog) { voices.resize (m == KeysMode::Organ ? 24 : 40); }

    const char* engineName() const override
    {
        switch (mode)
        {
            case KeysMode::Piano: return "Modal Piano";
            case KeysMode::EPiano: return "FM Tine Piano";
            case KeysMode::Organ: return "Tonewheel Organ";
            case KeysMode::Mallets: return "Modal Mallets";
        }
        return "Keys";
    }

    void prepare (float sampleRate) override
    {
        for (size_t i = 0; i < voices.size(); ++i) voices[i].prepare (sampleRate, 0xA5A5u + (uint32_t) i * 7919u);
    }

    void noteOn (const NoteEvent& e, double secPerTick) override
    {
        const int key = (int) std::lround (e.pitch);
        KeysVoice* v = nullptr;
        for (auto& x : voices) if (x.active && x.key == key) { x.release(); }
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (! v)
        {
            v = &voices[0];
            for (auto& x : voices) if (x.age < v->age) v = &x;
        }
        v->start (mode, program, e, secPerTick, ++stamp);
    }

    void noteOff (int midiKey) override
    {
        for (auto& v : voices) if (v.active && v.liveKey == midiKey) { v.liveKey = -1; v.release(); }
    }

    void allNotesOff (bool immediate) override
    {
        for (auto& v : voices) { if (immediate) v.kill(); else v.release(); }
    }

    void render (float* L, float* R, int n) override
    {
        for (auto& v : voices) if (v.active) v.render (L, R, n);
    }

private:
    KeysMode mode;
    int program;
    std::vector<KeysVoice> voices;
    uint64_t stamp = 0;
};
} // namespace

std::unique_ptr<Instrument> makePianoInstrument (Family f)
{
    switch (f)
    {
        case Family::EPiano: return std::make_unique<KeysInstrument> (KeysMode::EPiano, 4);
        case Family::Organ:  return std::make_unique<KeysInstrument> (KeysMode::Organ, 16);
        default:             return std::make_unique<KeysInstrument> (KeysMode::Piano, 0);
    }
}

std::unique_ptr<Instrument> makeMalletInstrument (int program)
{
    return std::make_unique<KeysInstrument> (KeysMode::Mallets, program);
}
} // namespace tc
