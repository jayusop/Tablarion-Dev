// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — small, allocation-free DSP building blocks
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tc::dsp
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float midiToHz (float m) { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }
inline float fastTanh (float x)
{
    x = std::clamp (x, -4.5f, 4.5f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float undenormal (float x) { return std::abs (x) < 1.0e-15f ? 0.0f : x; }

/** Fast deterministic white noise. */
struct Noise
{
    uint32_t s = 0x12345678u;
    explicit Noise (uint32_t seed = 0x12345678u) : s (seed ? seed : 1u) {}
    float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (int32_t) s * (1.0f / 2147483648.0f);
    }
};

struct OnePoleLP
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float hz, float sr) { a = std::exp (-kTwoPi * std::clamp (hz, 1.0f, sr * 0.49f) / sr); }
    float process (float x) { z = x + a * (z - x); return z; }
    void reset() { z = 0.0f; }
};

struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff (float hz, float sr) { lp.setCutoff (hz, sr); }
    float process (float x) { return x - lp.process (x); }
    void reset() { lp.reset(); }
};

/** RBJ biquad, transposed direct form II. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    enum Type { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf, Notch };

    void set (Type t, float f, float q, float gainDb, float sr)
    {
        f = std::clamp (f, 10.0f, sr * 0.45f);
        q = std::max (q, 0.05f);
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w = kTwoPi * f / sr, cw = std::cos (w), sw = std::sin (w);
        const float alpha = sw / (2.0f * q);
        float B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (t)
        {
            case LowPass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case HighPass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case BandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Notch:    B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case LowShelf:
            {
                const float s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case HighShelf:
            {
                const float s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }
};

/** Topology-preserving state variable filter (Zavalishin). */
struct SVF
{
    float g = 0, k = 1, ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0;
    void set (float hz, float q, float sr)
    {
        g = std::tan (kPi * std::clamp (hz, 10.0f, sr * 0.45f) / sr);
        k = 1.0f / std::max (q, 0.1f);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    // returns low-pass; band-pass in bp
    float process (float x, float& bp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        bp = v1;
        return v2;
    }
    float lp (float x) { float b; return process (x, b); }
    void reset() { ic1 = ic2 = 0; }
};

/** Fractional delay line with linear / cubic interpolation. */
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0;
    }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float read (float d) const // d >= 1
    {
        const float rp = (float) w - d;
        int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float a = buf[(size_t) (i & mask)], b = buf[(size_t) ((i + 1) & mask)];
        return a + (b - a) * f;
    }
    float readInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
};

/** Feedback-delay-network hall reverb: 8 modulated lines, Hadamard mixing, frequency-dependent decay. */
class FDNReverb
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        static const float baseMs[8] = { 37.1f, 41.9f, 47.3f, 53.7f, 59.3f, 67.1f, 73.9f, 79.7f };
        for (int i = 0; i < 8; ++i)
        {
            lines[i].prepare ((int) (sr * 0.2f));
            lens[i] = baseMs[i] * 0.001f * sr;
            damp[i].reset();
        }
        static const float apMs[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
        for (int i = 0; i < 4; ++i) { diff[i].prepare ((int) (sr * 0.02f)); apLen[i] = (int) (apMs[i] * 0.001f * sr); }
        pre.prepare ((int) (sr * 0.15f));
        inHP.setCutoff (160.0f, sr);
        update();
    }

    void setParams (float decaySeconds, float size, float dampingHz, float preDelayMs)
    {
        t60 = decaySeconds; roomSize = size; dampHz = dampingHz; preMs = preDelayMs;
        update();
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int s = 0; s < n; ++s)
        {
            float x = inHP.process ((inL[s] + inR[s]) * 0.5f);
            pre.push (x);
            x = pre.read (std::max (1.0f, preMs * 0.001f * sr));
            for (int i = 0; i < 4; ++i) // input diffusion
            {
                const float d = diff[i].readInt (apLen[i]);
                const float v = x + 0.62f * d;
                diff[i].push (v);
                x = d - 0.62f * v;
            }

            lfoPhase += lfoInc; if (lfoPhase > kTwoPi) lfoPhase -= kTwoPi;
            const float mod = std::sin (lfoPhase) * 0.0012f * sr;

            float y[8];
            for (int i = 0; i < 8; ++i)
            {
                float d = lens[i] * roomSize;
                if (i == 1) d += mod;
                if (i == 6) d -= mod;
                y[i] = damp[i].process (lines[i].read (std::max (2.0f, d))) * gains[i];
            }
            hadamard8 (y);
            static const float sign[8] = { 1, -1, 1, -1, 1, -1, 1, -1 };
            for (int i = 0; i < 8; ++i) lines[i].push (undenormal (y[i] + x * sign[i] * 0.35f));

            outL[s] = (y[0] + y[2] + y[4] + y[6]) * 0.5f;
            outR[s] = (y[1] + y[3] + y[5] + y[7]) * 0.5f;
        }
    }

    void clear()
    {
        for (auto& l : lines) l.clear();
        for (auto& d : diff) d.clear();
        pre.clear();
    }

private:
    float sr = 48000.0f, t60 = 2.2f, roomSize = 1.0f, dampHz = 7000.0f, preMs = 18.0f;
    DelayLine lines[8], diff[4], pre;
    float lens[8] {}, gains[8] {};
    int apLen[4] {};
    OnePoleLP damp[8];
    OnePoleHP inHP;
    float lfoPhase = 0.0f, lfoInc = 0.0f;

    void update()
    {
        for (int i = 0; i < 8; ++i)
        {
            const float lenSec = lens[i] * roomSize / sr;
            gains[i] = std::pow (10.0f, -3.0f * lenSec / std::max (0.1f, t60)) / 1.0f;
            damp[i].setCutoff (dampHz, sr);
        }
        lfoInc = kTwoPi * 0.31f / sr;
    }

    static void hadamard8 (float* x)
    {
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += h * 2)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b; x[j + h] = a - b;
                }
        const float norm = 0.35355339f; // 1/sqrt(8)
        for (int i = 0; i < 8; ++i) x[i] *= norm;
    }
};

/** Feed-forward compressor with soft knee. */
class Compressor
{
public:
    void prepare (float sampleRate) { sr = sampleRate; set (threshold, ratio, attackMs, releaseMs, makeup); }
    void set (float thresholdDb, float r, float aMs, float rMs, float makeupDb)
    {
        threshold = thresholdDb; ratio = r; attackMs = aMs; releaseMs = rMs; makeup = makeupDb;
        att = std::exp (-1.0f / (0.001f * aMs * sr));
        rel = std::exp (-1.0f / (0.001f * rMs * sr));
        makeupGain = dbToGain (makeupDb);
    }
    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float peak = std::max (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f);
            const float db = gainToDb (peak);
            float over = db - threshold;
            float gr = 0.0f;
            const float knee = 6.0f;
            if (over > knee * 0.5f) gr = over * (1.0f - 1.0f / ratio);
            else if (over > -knee * 0.5f) { const float t = over + knee * 0.5f; gr = (1.0f - 1.0f / ratio) * t * t / (2.0f * knee); }
            env = gr > env ? att * env + (1 - att) * gr : rel * env + (1 - rel) * gr;
            const float g = dbToGain (-env) * makeupGain;
            L[i] *= g;
            if (R) R[i] *= g;
        }
    }
    float gainReductionDb() const { return env; }

private:
    float sr = 48000, threshold = -18, ratio = 3, attackMs = 10, releaseMs = 120, makeup = 0;
    float att = 0, rel = 0, makeupGain = 1, env = 0;
};

/** Look-ahead brickwall limiter. */
class Limiter
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        look = std::max (1, (int) (0.0015f * sr));
        dl.prepare (look + 2); dr.prepare (look + 2);
        rel = std::exp (-1.0f / (0.12f * sr));
        att = std::exp (-1.0f / (look * 0.5f));
        gain = 1.0f; target = 1.0f;
    }
    void setCeiling (float db) { ceiling = dbToGain (db); }
    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float peak = std::max (std::abs (L[i]), std::abs (R[i]));
            const float need = peak > ceiling ? ceiling / peak : 1.0f;
            if (need < hold) { hold = need; holdCount = look; }
            else if (--holdCount <= 0) hold = 1.0f;
            target = hold;
            gain = target < gain ? att * gain + (1 - att) * target : rel * gain + (1 - rel) * target;
            dl.push (L[i]); dr.push (R[i]);
            const float g = std::min (gain, 1.0f);
            L[i] = std::clamp (dl.readInt (look) * g, -ceiling, ceiling);
            R[i] = std::clamp (dr.readInt (look) * g, -ceiling, ceiling);
        }
    }
private:
    float sr = 48000, ceiling = 0.97f, gain = 1, target = 1, hold = 1, att = 0.5f, rel = 0.999f;
    int look = 64, holdCount = 0;
    DelayLine dl, dr;
};

/** Stereo chorus (two modulated taps). */
class Chorus
{
public:
    void prepare (float sampleRate) { sr = sampleRate; l.prepare ((int) (sr * 0.05f)); r.prepare ((int) (sr * 0.05f)); }
    void set (float rateHz, float depthMs, float mix_) { inc = kTwoPi * rateHz / sr; depth = depthMs * 0.001f * sr; mix = mix_; }
    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            ph += inc; if (ph > kTwoPi) ph -= kTwoPi;
            l.push (L[i]); r.push (R[i]);
            const float base = 0.008f * sr;
            const float wl = l.read (base + depth * (1.0f + std::sin (ph)));
            const float wr = r.read (base + depth * (1.0f + std::sin (ph + 1.9f)));
            L[i] += mix * wl; R[i] += mix * wr;
        }
    }
private:
    float sr = 48000, inc = 0, depth = 0, mix = 0, ph = 0;
    DelayLine l, r;
};
} // namespace tc::dsp
