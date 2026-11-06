// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — virtual-analogue ensemble voices (strings, brass, reeds, flutes, choir, pads, leads, synth bass)
#include "Instrument.h"

namespace tc
{
namespace
{
using namespace dsp;

inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

struct Patch
{
    enum Wave { Saw, Square, Tri, Sine };
    Wave w1 = Saw, w2 = Saw;
    float mix2 = 0.5f, detune2 = 7.0f, octave2 = 0.0f;
    int   unison = 1;     float unisonCents = 8.0f;
    float noise = 0.0f;
    float cutoff = 3000.0f, reso = 0.7f, velToCutoff = 2000.0f, keyTrack = 0.5f;
    float fEnvAmt = 0.0f, fA = 0.01f, fD = 0.3f, fS = 1.0f;
    float aA = 0.01f, aD = 0.2f, aS = 1.0f, aR = 0.2f;
    float vibRate = 5.2f, vibDepth = 0.0f, vibDelay = 0.3f;
    bool  formant = false;
    float level = 0.25f;
    float pan = 0.0f;
};

Patch patchFor (Family f, int program)
{
    Patch p;
    switch (f)
    {
        case Family::Strings:
            p.unison = 3; p.unisonCents = 9.0f; p.mix2 = 0.0f; p.cutoff = 1900.0f; p.velToCutoff = 3500.0f; p.reso = 0.6f;
            p.aA = program == 45 ? 0.002f : 0.14f; p.aD = program == 45 ? 0.25f : 0.3f; p.aS = program == 45 ? 0.0f : 0.92f;
            p.aR = program == 45 ? 0.2f : 0.45f; p.vibDepth = 0.11f; p.level = 0.17f; break;
        case Family::Brass:
            p.unison = 2; p.unisonCents = 6.0f; p.mix2 = 0.0f; p.cutoff = 450.0f; p.fEnvAmt = 3800.0f; p.fA = 0.05f; p.fD = 0.35f; p.fS = 0.55f;
            p.velToCutoff = 2000.0f; p.reso = 0.8f; p.aA = 0.035f; p.aS = 0.9f; p.aR = 0.14f; p.vibDepth = 0.06f; p.level = 0.2f; break;
        case Family::Reed:
            p.w1 = Patch::Square; p.w2 = Patch::Saw; p.mix2 = 0.35f; p.detune2 = 3.0f; p.cutoff = 1500.0f; p.velToCutoff = 2500.0f;
            p.reso = 1.2f; p.noise = 0.04f; p.aA = 0.04f; p.aS = 0.95f; p.aR = 0.1f; p.vibDepth = 0.1f; p.vibDelay = 0.4f; p.level = 0.3f; break;
        case Family::Pipe:
            p.w1 = Patch::Sine; p.w2 = Patch::Tri; p.mix2 = 0.18f; p.detune2 = 0.0f; p.octave2 = 1.0f; p.cutoff = 4000.0f; p.velToCutoff = 2000.0f;
            p.noise = 0.07f; p.aA = 0.07f; p.aS = 0.95f; p.aR = 0.12f; p.vibDepth = 0.08f; p.level = 0.3f; break;
        case Family::Choir:
            p.unison = 3; p.unisonCents = 7.0f; p.mix2 = 0.0f; p.cutoff = 3000.0f; p.formant = true; p.aA = 0.22f; p.aS = 1.0f; p.aR = 0.5f;
            p.vibDepth = 0.09f; p.level = 0.28f; break;
        case Family::SynthLead:
            p.w1 = Patch::Square; p.w2 = Patch::Saw; p.mix2 = 0.5f; p.detune2 = 8.0f; p.cutoff = 3200.0f; p.reso = 1.6f; p.velToCutoff = 2000.0f;
            p.aA = 0.004f; p.aS = 1.0f; p.aR = 0.12f; p.vibDepth = 0.08f; p.level = 0.16f; break;
        case Family::SynthPad:
            p.unison = 3; p.unisonCents = 14.0f; p.mix2 = 0.0f; p.cutoff = 1400.0f; p.reso = 0.9f; p.aA = 0.6f; p.aS = 1.0f; p.aR = 1.3f; p.level = 0.15f; break;
        case Family::SynthBass:
            p.w2 = Patch::Square; p.mix2 = 0.5f; p.octave2 = -1.0f; p.detune2 = 0.0f; p.cutoff = 300.0f; p.fEnvAmt = 2400.0f; p.fA = 0.002f; p.fD = 0.22f; p.fS = 0.15f;
            p.reso = 2.2f; p.aA = 0.002f; p.aS = 1.0f; p.aR = 0.08f; p.level = 0.3f; break;
        default:
            p.mix2 = 0.4f; p.w2 = Patch::Square; p.cutoff = 2500.0f; p.aA = 0.01f; p.aR = 0.25f; p.level = 0.2f; break;
    }
    return p;
}

struct ADSR
{
    float a = 0.01f, d = 0.2f, s = 1.0f, r = 0.2f, v = 0.0f;
    int stage = 0; // 0 idle 1 attack 2 decay 3 sustain 4 release
    float sr = 48000.0f;
    void gate (bool on) { stage = on ? 1 : (stage == 0 ? 0 : 4); }
    float next()
    {
        switch (stage)
        {
            case 1: v += 1.0f / std::max (1.0f, a * sr); if (v >= 1.0f) { v = 1.0f; stage = 2; } break;
            case 2: v += (s - v) * (1.0f - std::exp (-4.0f / std::max (1.0f, d * sr))); if (std::abs (v - s) < 1.0e-4f) stage = 3; break;
            case 3: v = s; break;
            case 4: v *= std::exp (-6.9f / std::max (1.0f, r * sr)); if (v < 1.0e-5f) { v = 0.0f; stage = 0; } break;
            default: break;
        }
        return v;
    }
};

class SynthVoice
{
public:
    bool active = false;
    int key = -1, liveKey = -1;
    uint64_t age = 0;

    void prepare (float sampleRate, uint32_t seed) { sr = sampleRate; noise = Noise (seed); amp.sr = filt.sr = sr; }

    void start (const Patch& p, const NoteEvent& e, double secPerTick, uint64_t stamp)
    {
        patch = &p; age = stamp;
        key = (int) std::lround (e.pitch);
        liveKey = (e.flags & EvLive) ? key : -1;
        base = e.pitch; curve = e.curve; vel = e.velocity;
        durSec = (float) std::max (0.02, e.durTicks * secPerTick);
        curveSec = (float) std::max (0.02, e.curveTicks * secPerTick);
        t = 0.0f; released = false; active = true;
        amp.a = p.aA; amp.d = p.aD; amp.s = p.aS; amp.r = p.aR; amp.gate (true);
        filt.a = p.fA; filt.d = p.fD; filt.s = p.fS; filt.r = p.aR; filt.v = 0.0f; filt.gate (true);
        if (! wasActiveRecently()) { amp.v = 0.0f; }
        for (int i = 0; i < 4; ++i) phase[i] = (float) ((key * 31 + i * 17) % 100) / 100.0f;
        svf.reset(); f1.reset(); f2.reset();
        f1.set (730.0f, 6.0f, sr); f2.set (1150.0f, 7.0f, sr);
        ctrl = 0;
    }

    void release() { if (! released) { released = true; amp.gate (false); filt.gate (false); } }
    void kill() { active = false; amp.stage = 0; amp.v = 0.0f; }
    void setBend (float b) { bend = b; }

    void render (float* L, float* R, int n)
    {
        if (! active) return;
        const Patch& p = *patch;
        for (int s = 0; s < n; ++s)
        {
            if (--ctrl <= 0)
            {
                ctrl = 32;
                const float x = std::min (1.0f, t / curveSec);
                float semis = base + curve.eval (x, t) + bend;
                if (p.vibDepth > 0.0f && t > p.vibDelay)
                    semis += p.vibDepth * std::min (1.0f, (t - p.vibDelay) * 2.0f) * std::sin (kTwoPi * p.vibRate * t);
                freq = midiToHz (semis);
                t += 32.0f / sr;
                const float fe = filt.v;
                float fc = p.cutoff + p.velToCutoff * vel + p.fEnvAmt * fe * (0.4f + 0.6f * vel);
                fc *= std::exp2 ((semis - 60.0f) / 12.0f * p.keyTrack);
                svf.set (std::min (fc, sr * 0.45f), p.reso, sr);
            }
            filt.next();
            const float a = amp.next();
            if (amp.stage == 0 && released) { active = false; break; }

            float osc = 0.0f;
            const int uni = std::max (1, p.unison);
            for (int u = 0; u < uni; ++u)
            {
                const float det = uni == 1 ? 0.0f : p.unisonCents * (2.0f * (float) u / (float) (uni - 1) - 1.0f);
                const float f = freq * std::exp2 (det / 1200.0f);
                osc += wave (p.w1, phase[u], f / sr);
            }
            osc /= std::sqrt ((float) uni);
            if (p.mix2 > 0.0f)
            {
                const float f = freq * std::exp2 (p.octave2 + p.detune2 / 1200.0f);
                osc = osc * (1.0f - p.mix2) + wave (p.w2, phase[3], f / sr) * p.mix2;
            }
            if (p.noise > 0.0f) osc += noise.next() * p.noise * (0.5f + a);

            float y = svf.lp (osc);
            if (p.formant)
            {
                float b1, b2;
                f1.process (y, b1); f2.process (y, b2);
                y = y * 0.25f + b1 * 1.2f + b2 * 0.8f;
            }
            y *= a * p.level * (0.35f + 0.65f * vel);
            L[s] += y; R[s] += y;
        }
        if (! released && liveKey < 0 && t >= durSec) release();
    }

private:
    float sr = 48000.0f;
    const Patch* patch = nullptr;
    ADSR amp, filt;
    SVF svf, f1, f2;
    Noise noise;
    float phase[4] {};
    float base = 60.0f, vel = 0.7f, freq = 440.0f, bend = 0.0f;
    PitchCurve curve;
    float t = 0.0f, durSec = 1.0f, curveSec = 1.0f;
    bool released = false;
    int ctrl = 0;

    bool wasActiveRecently() const { return false; }

    float wave (Patch::Wave w, float& ph, float dt)
    {
        ph += dt;
        if (ph >= 1.0f) ph -= 1.0f;
        switch (w)
        {
            case Patch::Saw: return 2.0f * ph - 1.0f - polyBlep (ph, dt);
            case Patch::Square:
            {
                float v = ph < 0.5f ? 1.0f : -1.0f;
                v += polyBlep (ph, dt);
                float p2 = ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
                v -= polyBlep (p2, dt);
                return v * 0.7f;
            }
            case Patch::Tri: return (4.0f * std::abs (ph - 0.5f) - 1.0f);
            case Patch::Sine: return std::sin (kTwoPi * ph);
        }
        return 0.0f;
    }
};

class SynthInstrument final : public Instrument
{
public:
    SynthInstrument (Family f, int program) : patch (patchFor (f, program)), family (f) { voices.resize (24); }

    const char* engineName() const override { return "VA Ensemble"; }

    void prepare (float sampleRate) override
    {
        for (size_t i = 0; i < voices.size(); ++i) voices[i].prepare (sampleRate, 0x51u + (uint32_t) i * 104729u);
    }

    void noteOn (const NoteEvent& e, double secPerTick) override
    {
        const int key = (int) std::lround (e.pitch);
        for (auto& v : voices) if (v.active && v.key == key) v.release();
        SynthVoice* v = nullptr;
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; }
        v->start (patch, e, secPerTick, ++stamp);
        v->setBend (bend);
    }

    void noteOff (int midiKey) override
    {
        for (auto& v : voices) if (v.active && v.liveKey == midiKey) { v.liveKey = -1; v.release(); }
    }

    void allNotesOff (bool immediate) override { for (auto& v : voices) { if (immediate) v.kill(); else v.release(); } }
    void setPitchBend (float semis) override { bend = semis; for (auto& v : voices) v.setBend (semis); }

    void render (float* L, float* R, int n) override
    {
        for (auto& v : voices) if (v.active) v.render (L, R, n);
    }

private:
    Patch patch;
    Family family;
    std::vector<SynthVoice> voices;
    uint64_t stamp = 0;
    float bend = 0.0f;
};
} // namespace

std::unique_ptr<Instrument> makeSynthInstrument (Family f, int program)
{
    return std::make_unique<SynthInstrument> (f, program);
}

std::unique_ptr<Instrument> makeModeledInstrument (const Track& track)
{
    const Family f = track.family;
    if (f == Family::Drums) return makeDrumInstrument();
    if (isStringModelFamily (f)) return makeStringInstrument (track);
    switch (f)
    {
        case Family::Piano: case Family::EPiano: case Family::Organ: return makePianoInstrument (f);
        case Family::Mallets: return makeMalletInstrument (track.program);
        default: return makeSynthInstrument (f, track.program);
    }
}
} // namespace tc
