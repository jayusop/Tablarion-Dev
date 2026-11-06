// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — physically modelled plucked strings (guitars, basses, banjo, mandolin, harp...)
//
// Each string is a dual-polarisation digital waveguide (extended Karplus-Strong):
//   - 4-point Lagrange fractional delay -> exact tuning and smooth bends/slides
//   - one-pole loss filter + allpass dispersion (string stiffness), with phase-delay compensation
//   - frequency-dependent T60 decay, palm-mute / finger damping, pickup-position comb
//   - excitation shaped by pluck position, velocity-dependent brightness, pick noise
// One voice per string, exactly like a real instrument: re-plucking a ringing string damps it,
// legato notes (hammer-on, pull-off, legato slide) re-tune the same vibrating string.
#include "Instrument.h"

namespace tc
{
namespace
{
using namespace dsp;

struct StringParams
{
    float t60Ref     = 7.0f;   // seconds at 82.4 Hz
    float t60Exp     = 0.55f;  // T60 ~ (f / 82.4)^-exp
    float loopLP     = 0.10f;  // loss filter coefficient (higher = darker)
    float dispersion = -0.10f; // allpass coefficient (negative = stiff string)
    float pluckPos   = 0.13f;
    float pickupPos  = 0.0f;   // 0 = acoustic (no pickup comb)
    float brightness = 1.0f;
    float pickNoise  = 0.06f;
    float detune     = 0.0007f;
    float releaseT60 = 0.09f;
    float level      = 0.7f;
    float spread     = 0.12f;  // stereo spread across strings
};

StringParams paramsFor (Family f)
{
    StringParams p;
    switch (f)
    {
        case Family::SteelGuitar: break;
        case Family::NylonGuitar:
            p.t60Ref = 4.5f; p.t60Exp = 0.6f; p.loopLP = 0.26f; p.dispersion = -0.03f; p.pluckPos = 0.17f;
            p.brightness = 0.55f; p.pickNoise = 0.025f; p.level = 0.42f; break;
        case Family::CleanElectric:
            p.t60Ref = 9.0f; p.t60Exp = 0.5f; p.loopLP = 0.07f; p.dispersion = -0.08f; p.pluckPos = 0.12f;
            p.pickupPos = 0.14f; p.brightness = 0.85f; p.pickNoise = 0.03f; break;
        case Family::JazzElectric:
            p.t60Ref = 6.5f; p.loopLP = 0.2f; p.pickupPos = 0.24f; p.brightness = 0.55f; p.pickNoise = 0.02f; p.pluckPos = 0.2f; break;
        case Family::MutedElectric:
            p.t60Ref = 0.35f; p.loopLP = 0.45f; p.pickupPos = 0.12f; p.brightness = 0.6f; break;
        case Family::OverdriveGuitar:
        case Family::DistortionGuitar:
            p.t60Ref = 12.0f; p.t60Exp = 0.45f; p.loopLP = 0.06f; p.dispersion = -0.08f; p.pickupPos = 0.1f;
            p.pluckPos = 0.11f; p.brightness = 0.95f; p.pickNoise = 0.02f; p.releaseT60 = 0.07f; break;
        case Family::HarmonicsGuitar:
            p.loopLP = 0.15f; p.pickupPos = 0.12f; break;
        case Family::FingerBass:
            p.t60Ref = 6.0f; p.t60Exp = 0.5f; p.loopLP = 0.2f; p.dispersion = -0.18f; p.pluckPos = 0.2f; p.pickupPos = 0.16f;
            p.brightness = 0.5f; p.pickNoise = 0.015f; p.releaseT60 = 0.13f; p.level = 0.75f; p.spread = 0.0f; break;
        case Family::PickBass:
            p.t60Ref = 6.0f; p.loopLP = 0.12f; p.dispersion = -0.2f; p.pluckPos = 0.12f; p.pickupPos = 0.12f;
            p.brightness = 0.8f; p.pickNoise = 0.05f; p.releaseT60 = 0.12f; p.level = 0.7f; p.spread = 0.0f; break;
        case Family::SlapBass:
            p.t60Ref = 5.0f; p.loopLP = 0.09f; p.dispersion = -0.22f; p.pluckPos = 0.09f; p.pickupPos = 0.12f;
            p.brightness = 1.1f; p.pickNoise = 0.05f; p.releaseT60 = 0.1f; p.level = 0.68f; p.spread = 0.0f; break;
        case Family::FretlessBass:
            p.t60Ref = 7.0f; p.loopLP = 0.3f; p.dispersion = -0.12f; p.pluckPos = 0.22f; p.pickupPos = 0.2f;
            p.brightness = 0.45f; p.pickNoise = 0.01f; p.releaseT60 = 0.15f; p.level = 0.78f; p.spread = 0.0f; break;
        case Family::AcousticBass:
            p.t60Ref = 3.0f; p.loopLP = 0.3f; p.dispersion = -0.1f; p.pluckPos = 0.22f;
            p.brightness = 0.45f; p.pickNoise = 0.02f; p.releaseT60 = 0.15f; p.level = 0.8f; p.spread = 0.0f; break;
        case Family::SynthBass:
            p.t60Ref = 4.0f; p.loopLP = 0.15f; p.pickupPos = 0.15f; p.level = 0.7f; p.spread = 0.0f; break;
        case Family::Banjo:
            p.t60Ref = 1.6f; p.loopLP = 0.04f; p.dispersion = -0.05f; p.pluckPos = 0.08f; p.brightness = 1.2f; p.pickNoise = 0.08f; break;
        case Family::Mandolin:
            p.t60Ref = 2.4f; p.loopLP = 0.06f; p.pluckPos = 0.1f; p.brightness = 1.1f; p.detune = 0.0022f; p.pickNoise = 0.08f; break;
        case Family::Ukulele:
            p.t60Ref = 2.4f; p.loopLP = 0.24f; p.dispersion = -0.03f; p.pluckPos = 0.18f; p.brightness = 0.6f; p.pickNoise = 0.02f; break;
        case Family::Sitar:
            p.t60Ref = 5.0f; p.loopLP = 0.04f; p.pluckPos = 0.07f; p.brightness = 1.3f; p.detune = 0.0015f; break;
        case Family::Harp:
            p.t60Ref = 6.0f; p.loopLP = 0.22f; p.dispersion = -0.03f; p.pluckPos = 0.3f; p.brightness = 0.5f;
            p.pickNoise = 0.0f; p.releaseT60 = 1.5f; p.spread = 0.5f; break;
        default: break;
    }
    return p;
}

inline void lagrangeCoeffs (float x, float* h)
{
    // points at -1, 0, 1, 2 ; x in [0, 1)
    const float xm1 = x - 1.0f, xm2 = x - 2.0f, xp1 = x + 1.0f;
    h[0] = -x * xm1 * xm2 * (1.0f / 6.0f);
    h[1] = xp1 * xm1 * xm2 * 0.5f;
    h[2] = -xp1 * x * xm2 * 0.5f;
    h[3] = xp1 * x * xm1 * (1.0f / 6.0f);
}

class StringVoice
{
public:
    static constexpr int kBuf = 8192, kMask = kBuf - 1, kCtrl = 16;

    StringVoice() : bufA ((size_t) kBuf, 0.0f), bufB ((size_t) kBuf, 0.0f), exc ((size_t) kBuf, 0.0f) {}

    void prepare (float sampleRate, const StringParams& p, float pan, uint32_t seed)
    {
        sr = sampleRate; params = p; noise = Noise (seed);
        const float a = (pan + 1.0f) * 0.25f * kPi;
        panL = std::cos (a); panR = std::sin (a);
        clickHP.setCutoff (1800.0f, sr);
        postLP.setCutoff (18000.0f, sr);
    }

    bool active = false;
    int  liveKey = -1;
    uint64_t age = 0;

    void start (const NoteEvent& e, double secPerTick, bool legato, uint64_t stamp)
    {
        age = stamp;
        flags = e.flags;
        liveKey = (e.flags & EvLive) ? (int) std::lround (e.pitch) : -1;
        const bool wasActive = active;
        const float oldPitch = basePitch;

        basePitch = e.pitch;
        curve = e.curve;
        t = 0.0f;
        durSec = (float) std::max (0.005, e.durTicks * secPerTick);
        curveSec = (float) std::max (0.005, e.curveTicks * secPerTick);
        released = false;
        fade = 1.0f;
        velocity = e.velocity;

        // damping / colour for this note
        float t60Scale = 1.0f, lp = params.loopLP, bright = params.brightness;
        if (flags & EvPalmMute) { t60Scale = -1.0f; lp = std::max (lp, 0.5f); bright *= 0.45f; }
        if (flags & EvDead)     { t60Scale = -2.0f; lp = 0.6f; }
        if (flags & EvHarmonic) { t60Scale = 1.3f; lp = std::min (lp, 0.08f); }
        if (flags & EvSlap)     { bright *= 1.6f; }
        if (flags & EvPop)      { bright *= 2.0f; }
        if (flags & EvTapped)   { bright *= 0.65f; }
        if (flags & EvUpStroke) { bright *= 0.9f; }
        noteT60Scale = t60Scale;
        // high-frequency loss is roughly per unit time: low strings (fewer loop passes) need a darker loop filter
        const float fNote = dsp::midiToHz (basePitch);
        if (t60Scale > 0.0f) lp *= std::clamp (std::sqrt (330.0f / fNote), 0.5f, 2.2f);
        loopA = std::clamp (lp, 0.0f, 0.85f);
        apC = params.dispersion;
        currentT60 = targetT60 (dsp::midiToHz (basePitch));

        if (legato && wasActive)
        {
            pitchGlideFrom = oldPitch - basePitch; // short glide (fretting finger lands)
            glideSamples = (int) (0.004f * sr);
            glidePos = 0;
            float amt = 0.0f, br = 0.5f;
            if (flags & EvHammerOn) { amt = 0.22f; br = 0.45f; }
            if (flags & EvPullOff)  { amt = 0.32f; br = 0.75f; }
            if (amt > 0.0f) makeExcitation (velocity * amt, br, 0.5f, false, false);
        }
        else
        {
            pitchGlideFrom = 0.0f; glideSamples = 0;
            if (wasActive)
            {
                // the pick/finger touches the vibrating string before plucking it
                const float damp = std::abs (oldPitch - basePitch) > 0.5f ? 0.12f : 0.45f;
                for (auto& s : bufA) s *= damp;
                for (auto& s : bufB) s *= damp;
            }
            else
            {
                std::fill (bufA.begin(), bufA.end(), 0.0f);
                std::fill (bufB.begin(), bufB.end(), 0.0f);
                lpA = lpB = apxA = apyA = apxB = apyB = 0.0f;
            }
            const bool harmonic = (flags & EvHarmonic) != 0;
            const bool dead = (flags & EvDead) != 0;
            const float pluck = harmonic ? 0.5f : params.pluckPos * ((flags & EvUpStroke) ? 1.15f : 1.0f);
            makeExcitation (std::pow (velocity, 1.1f), bright * (0.35f + 0.65f * velocity), pluck, harmonic, dead);

            float click = params.pickNoise * velocity;
            if (flags & EvTapped) click *= 0.2f;
            if (flags & EvSlap) click = 0.25f * velocity;
            if (flags & EvPop) click = 0.3f * velocity;
            if (dead) click = std::max (click, 0.12f * velocity);
            clickAmp = click;
            clickDecay = std::exp (-1.0f / (0.0016f * sr * (dead ? 4.0f : 1.0f)));
        }

        postLP.setCutoff ((flags & EvPalmMute) ? 2600.0f : 18000.0f, sr);
        if (flags & EvPinch) postLP.setCutoff (18000.0f, sr);
        active = true;
        controlCountdown = 0;
        updateControl (true);
    }

    void release()
    {
        if (! released) { released = true; }
    }

    void kill() { active = false; }

    void setLiveBend (float semis) { liveBend = semis; }

    void render (float* L, float* R, int n)
    {
        if (! active) return;
        for (int i = 0; i < n; ++i)
        {
            if (--controlCountdown <= 0) updateControl (false);

            const float D = dCur; dCur += dInc;
            const float DB = D * (1.0f - params.detune);

            const float yA = readLagrange (bufA, D);
            const float yB = readLagrange (bufB, DB);

            lpA = (1.0f - loopA) * yA + loopA * lpA;
            lpB = (1.0f - loopA) * yB + loopA * lpB;
            const float dA = apC * lpA + apxA - apC * apyA; apxA = lpA; apyA = dA;
            const float dB = apC * lpB + apxB - apC * apyB; apxB = lpB; apyB = dB;

            float in = 0.0f;
            if (excPos < excLen) in = exc[(size_t) excPos++];

            bufA[(size_t) w] = undenormal (gA * dA + in);
            bufB[(size_t) w] = undenormal (gB * dB + in * 0.6f);
            w = (w + 1) & kMask;

            float y = yA * 0.62f + yB * 0.38f;
            if (params.pickupPos > 0.0f)
                y = y - 0.85f * readLagrange (bufA, std::max (2.5f, D * params.pickupPos));

            if (clickAmp > 1.0e-5f)
            {
                y += clickHP.process (noise.next()) * clickAmp;
                clickAmp *= clickDecay;
            }
            y = postLP.process (y) * params.level * fade;

            env = std::max (std::abs (y), env * 0.9997f);
            L[i] += y * panL;
            R[i] += y * panR;
        }

        if (! released && ! (flags & EvLive) && t >= durSec) release();
        if (env < 1.0e-5f && (released || t > 0.3f)) active = false;
        if (released && t > durSec + 4.0f) active = false;
    }

private:
    float sr = 48000.0f;
    StringParams params;
    Noise noise;
    std::vector<float> bufA, bufB, exc;
    int w = 0, excPos = 0, excLen = 0;
    float lpA = 0, lpB = 0, apxA = 0, apyA = 0, apxB = 0, apyB = 0;
    float loopA = 0.1f, apC = -0.1f, gA = 0.99f, gB = 0.99f;
    float dCur = 100.0f, dInc = 0.0f;
    float basePitch = 60.0f, velocity = 0.7f, liveBend = 0.0f;
    PitchCurve curve;
    float t = 0.0f, durSec = 1.0f, curveSec = 1.0f;
    bool released = false;
    uint32_t flags = 0;
    float noteT60Scale = 1.0f, currentT60 = 5.0f;
    float pitchGlideFrom = 0.0f; int glideSamples = 0, glidePos = 0;
    float clickAmp = 0.0f, clickDecay = 0.99f;
    OnePoleHP clickHP;
    OnePoleLP postLP;
    float panL = 0.707f, panR = 0.707f, env = 0.0f, fade = 1.0f;
    int controlCountdown = 0;

    float readLagrange (const std::vector<float>& b, float d) const
    {
        const int di = (int) d;
        const float frac = d - (float) di;
        float h[4];
        lagrangeCoeffs (frac, h);
        // delay (di - 1) .. (di + 2)
        const int base = w - di;
        return h[0] * b[(size_t) ((base + 1) & kMask)] + h[1] * b[(size_t) (base & kMask)]
             + h[2] * b[(size_t) ((base - 1) & kMask)] + h[3] * b[(size_t) ((base - 2) & kMask)];
    }

    float targetT60 (float f) const
    {
        if (noteT60Scale == -1.0f) return 0.22f + 0.1f * std::clamp (200.0f / f, 0.0f, 1.0f);
        if (noteT60Scale == -2.0f) return 0.03f;
        return params.t60Ref * noteT60Scale * std::pow (std::max (f, 20.0f) / 82.4f, -params.t60Exp);
    }

    void makeExcitation (float amp, float bright, float pluckPos, bool harmonic, bool dead)
    {
        const float f = dsp::midiToHz (basePitch);
        int len = std::clamp ((int) std::lround (sr / f), 4, kBuf / 2);
        const int pp = std::clamp ((int) std::lround (pluckPos * (float) len), 1, len - 1);
        const float noiseMix = dead ? 0.85f : (harmonic ? 0.0f : 0.18f * std::min (1.5f, bright));

        OnePoleLP lp1, lp2;
        const float fc = std::clamp (700.0f + 9000.0f * bright * bright, 300.0f, sr * 0.45f);
        lp1.setCutoff (fc, sr); lp2.setCutoff (fc * 1.4f, sr);

        // if the previous excitation is still being injected, mix into it
        const int start = excPos < excLen ? excPos : 0;
        if (excPos >= excLen) { excPos = 0; excLen = 0; }

        float mean = 0.0f;
        std::vector<float>& e = exc;
        const int total = len + 24; // filter tail
        float tmp[kBuf / 2 + 32];
        for (int i = 0; i < total && i < kBuf / 2 + 32; ++i)
        {
            float tri = 0.0f;
            if (i < len) tri = i < pp ? (float) i / (float) pp : (float) (len - i) / (float) (len - pp);
            const float nz = i < len ? noise.next() : 0.0f;
            float v = (1.0f - noiseMix) * tri + noiseMix * nz;
            v = lp2.process (lp1.process (v));
            tmp[i] = v;
            mean += v;
        }
        mean /= (float) total;
        float peak = 1.0e-6f;
        for (int i = 0; i < total; ++i) { tmp[i] -= mean; peak = std::max (peak, std::abs (tmp[i])); }
        const float g = amp * 0.9f / peak;
        for (int i = 0; i < total; ++i)
        {
            const int k = start + i;
            if (k >= kBuf) break;
            if (k < excLen) e[(size_t) k] += tmp[i] * g;
            else e[(size_t) k] = tmp[i] * g;
        }
        excLen = std::max (excLen, std::min (kBuf, start + total));
    }

    void updateControl (bool snap)
    {
        controlCountdown = kCtrl;
        const float x = std::min (1.0f, t / curveSec);
        float semis = basePitch + curve.eval (x, t) + liveBend;
        if (glidePos < glideSamples)
        {
            semis += pitchGlideFrom * (1.0f - (float) glidePos / (float) glideSamples);
            glidePos += kCtrl;
        }
        t += (float) kCtrl / sr; // note time advances at control rate

        const float f = std::clamp (dsp::midiToHz (semis), 20.0f, sr * 0.3f);
        const float wv = kTwoPi * f / sr;
        const float sw = std::sin (wv), cw = std::cos (wv);
        const float pdLP = std::atan2 (loopA * sw, 1.0f - loopA * cw) / wv;
        const float phAP = std::atan2 (-sw, apC + cw) - std::atan2 (-apC * sw, 1.0f + apC * cw);
        const float pdAP = -phAP / wv;
        float D = sr / f - pdLP - pdAP;
        D = std::clamp (D, 3.0f, (float) kBuf - 8.0f);

        if (snap) { dCur = D; dInc = 0.0f; }
        else dInc = (D - dCur) / (float) kCtrl;

        // decay
        float target = targetT60 (f);
        if (released) target = std::min (target, params.releaseT60);
        currentT60 += (target - currentT60) * (released ? 0.25f : 1.0f);
        const float magLP = (1.0f - loopA) / std::sqrt (1.0f - 2.0f * loopA * cw + loopA * loopA);
        const float gPer = std::pow (10.0f, -3.0f / (std::max (0.01f, currentT60) * f));
        gA = std::min (0.99995f, gPer / magLP);
        const float gPerB = std::pow (10.0f, -3.0f / (std::max (0.01f, currentT60 * 1.25f) * f));
        gB = std::min (0.99995f, gPerB / magLP);

        if ((flags & EvSlideOutFade) && x > curve.slideOutStart)
            fade = std::max (0.0f, 1.0f - (x - curve.slideOutStart) / std::max (0.05f, 1.0f - curve.slideOutStart));
    }
};

class StringInstrument final : public Instrument
{
public:
    explicit StringInstrument (const Track& track)
        : family (track.family), tuning (track.tuning), capo (track.capo)
    {
        if (tuning.empty())
            tuning = isBassFamily (family) ? std::vector<int> { 28, 33, 38, 43 } : std::vector<int> { 40, 45, 50, 55, 59, 64 };
        params = paramsFor (family);
        voices.resize (tuning.size() + kExtra);
    }

    const char* engineName() const override { return "Modeled String"; }

    void prepare (float sampleRate) override
    {
        sr = sampleRate;
        const int ns = (int) tuning.size();
        for (size_t i = 0; i < voices.size(); ++i)
        {
            float pan = 0.0f;
            if ((int) i < ns && ns > 1) pan = params.spread * (2.0f * (float) i / (float) (ns - 1) - 1.0f);
            else pan = params.spread * (((int) i % 3) - 1) * 0.5f;
            voices[i].prepare (sr, params, pan, 0x9E3779B9u * (uint32_t) (i + 1));
        }
    }

    void noteOn (const NoteEvent& e, double secPerTick) override
    {
        const bool legato = (e.flags & EvLegato) != 0;
        int idx = e.string;
        if (idx < 0 || idx >= (int) tuning.size()) idx = chooseString (e.pitch);
        StringVoice* v = idx >= 0 ? &voices[(size_t) idx] : nullptr;
        if (v == nullptr)
        {
            // extra pool (harp, notes outside the tuning)
            StringVoice* best = &voices[tuning.size()];
            for (size_t i = tuning.size(); i < voices.size(); ++i)
            {
                if (! voices[i].active) { best = &voices[i]; break; }
                if (voices[i].age < best->age) best = &voices[i];
            }
            v = best;
        }
        v->start (e, secPerTick, legato, ++stamp);
        v->setLiveBend (bend);
    }

    void noteOff (int midiKey) override
    {
        for (auto& v : voices)
            if (v.active && v.liveKey == midiKey) { v.liveKey = -1; v.release(); }
    }

    void allNotesOff (bool immediate) override
    {
        for (auto& v : voices) { if (immediate) v.kill(); else v.release(); }
    }

    void setPitchBend (float semis) override
    {
        bend = semis;
        for (auto& v : voices) v.setLiveBend (semis);
    }

    void render (float* L, float* R, int n) override
    {
        for (auto& v : voices)
            if (v.active)
                v.render (L, R, n);
    }

private:
    static constexpr size_t kExtra = 6;
    Family family;
    std::vector<int> tuning;
    int capo = 0;
    StringParams params;
    std::vector<StringVoice> voices;
    float sr = 48000.0f, bend = 0.0f;
    uint64_t stamp = 0;

    int chooseString (float pitch)
    {
        // live MIDI: pick the highest string that can play the note within 15 frets, prefer free strings
        int best = -1, bestScore = 1 << 30;
        for (int s = 0; s < (int) tuning.size(); ++s)
        {
            const float fret = pitch - (float) (tuning[(size_t) s] + capo);
            if (fret < 0.0f || fret > 24.0f) continue;
            int score = (int) fret + (voices[(size_t) s].active ? 20 : 0);
            if (score < bestScore) { bestScore = score; best = s; }
        }
        return best;
    }
};
} // namespace

std::unique_ptr<Instrument> makeStringInstrument (const Track& track)
{
    return std::make_unique<StringInstrument> (track);
}
} // namespace tc
