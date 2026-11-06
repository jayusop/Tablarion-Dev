// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — SoundFont 2 sample-playback instrument with guitar articulation support
#include "Instrument.h"
#include "SoundFont.h"

namespace tc
{
namespace
{
using namespace dsp;

class SFVoice
{
public:
    bool active = false;
    int key = -1, liveKey = -1, string = -1, exclusive = 0;
    uint64_t age = 0;

    void prepare (float sampleRate) { sr = sampleRate; }

    void start (const SoundFont::VoiceParams& p, const NoteEvent& e, double secPerTick, uint64_t stamp,
                float velScale, float fcScale, float maxDur)
    {
        vp = p; age = stamp; active = true; released = false;
        key = (int) std::lround (e.pitch);
        liveKey = (e.flags & EvLive) ? key : -1;
        string = e.string;
        exclusive = p.exclusiveClass;
        flags = e.flags;
        basePitch = vp.fixedKey >= 0 ? (float) vp.fixedKey : e.pitch;
        curve = e.curve;
        t = 0.0f;
        durSec = (float) std::max (0.01, std::min ((double) maxDur, e.durTicks * secPerTick));
        curveSec = (float) std::max (0.01, e.curveTicks * secPerTick);
        pos = (double) vp.start;
        const float v = vp.fixedVel >= 0 ? vp.fixedVel / 127.0f : std::clamp (e.velocity * velScale, 0.01f, 1.0f);
        const float velDb = 40.0f * std::log10 (std::max (v, 0.01f));
        gain = dbToGain (-vp.attenuationDb + velDb);
        fcBase = std::min (vp.filterFc, 20000.0f) * fcScale;
        if (vp.filterFc < 13000.0f) fcBase *= std::exp2 (-2400.0f * (1.0f - v) / 1200.0f);
        filterOn = fcBase < 16000.0f;
        svfL.reset();
        const float a = (vp.pan + 0.5f) * 0.5f * kPi;
        panL = std::cos (a); panR = std::sin (a);
        volStage = 0; volEnv = 0.0f; stageTime = 0.0f;
        modStage = 0; modEnv = 0.0f; modTime = 0.0f;
        fade = 1.0f;
        ctrl = 0;
    }

    void release() { if (! released) { released = true; volStage = 5; modStage = 5; } }
    void fastRelease() { release(); vp.releaseVol = std::min (vp.releaseVol, 0.04f); }
    void kill() { active = false; }
    void setBend (float b) { bend = b; }

    void retune (const NoteEvent& e, double secPerTick)
    {
        basePitch = e.pitch; curve = e.curve; t = 0.0f;
        key = (int) std::lround (e.pitch);
        durSec = (float) std::max (0.01, e.durTicks * secPerTick);
        curveSec = (float) std::max (0.01, e.curveTicks * secPerTick);
        flags = e.flags;
        if (released) { released = false; volStage = 3; }
    }

    void render (float* L, float* R, int n)
    {
        if (! active) return;
        const int16_t* d = vp.data;
        for (int s = 0; s < n; ++s)
        {
            if (--ctrl <= 0) control();

            // cubic hermite interpolation
            const uint32_t i = (uint32_t) pos;
            const float f = (float) (pos - (double) i);
            const float xm1 = d[i > 0 ? i - 1 : 0] * (1.0f / 32768.0f);
            const float x0 = d[i] * (1.0f / 32768.0f);
            const float x1 = d[i + 1] * (1.0f / 32768.0f);
            const float x2 = d[i + 2] * (1.0f / 32768.0f);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            float y = ((c3 * f + c2) * f + c1) * f + x0;

            pos += inc;
            const bool loop = vp.loopMode == 1 || (vp.loopMode == 3 && ! released);
            if (loop)
            {
                while (pos >= (double) vp.loopEnd) pos -= (double) (vp.loopEnd - vp.loopStart);
            }
            else if (pos >= (double) vp.end - 1.0)
            {
                active = false;
                break;
            }

            if (filterOn) y = svfL.lp (y);
            y *= volEnv * gain * fade;
            L[s] += y * panL;
            R[s] += y * panR;
        }
        if (! released && liveKey < 0 && t >= durSec) release();
    }

private:
    float sr = 48000.0f;
    SoundFont::VoiceParams vp;
    double pos = 0.0, inc = 1.0;
    bool released = false;
    uint32_t flags = 0;
    float basePitch = 60.0f, bend = 0.0f;
    PitchCurve curve;
    float t = 0.0f, durSec = 1.0f, curveSec = 1.0f;
    float gain = 1.0f, fcBase = 20000.0f, panL = 0.7f, panR = 0.7f, fade = 1.0f;
    bool filterOn = false;
    SVF svfL;
    int volStage = 0; float volEnv = 0.0f, stageTime = 0.0f;
    int modStage = 0; float modEnv = 0.0f, modTime = 0.0f;
    int ctrl = 0;
    static constexpr int kCtrl = 16;

    void advanceEnv (int& stage, float& env, float& time, float delay, float attack, float hold, float decay, float sustain, float rel, float dt, bool isVol)
    {
        time += dt;
        switch (stage)
        {
            case 0: if (time >= delay) { stage = 1; time = 0.0f; } env = 0.0f; break;
            case 1: env = attack <= 0.0011f ? 1.0f : std::min (1.0f, time / attack); if (env >= 1.0f) { stage = 2; time = 0.0f; } break;
            case 2: env = 1.0f; if (time >= hold) { stage = 3; time = 0.0f; } break;
            case 3:
            {
                if (isVol)
                {
                    // exponential (dB-linear) decay: 100 dB over `decay` seconds
                    const float susDb = gainToDb (std::max (sustain, 1.0e-5f));
                    const float db = std::max (susDb, -100.0f * time / std::max (0.001f, decay));
                    env = dbToGain (db);
                    if (db <= susDb) stage = 4;
                }
                else
                {
                    env = 1.0f - (1.0f - sustain) * std::min (1.0f, time / std::max (0.001f, decay));
                    if (time >= decay) stage = 4;
                }
                break;
            }
            case 4: env = sustain; break;
            case 5:
            {
                if (isVol)
                {
                    env *= std::pow (10.0f, -5.0f * dt / std::max (0.005f, rel)); // -100 dB over release
                    if (env < 1.0e-4f) { env = 0.0f; active = false; }
                }
                else env = std::max (0.0f, env - dt / std::max (0.005f, rel));
                break;
            }
            default: break;
        }
    }

    void control()
    {
        ctrl = kCtrl;
        const float dt = (float) kCtrl / sr;
        advanceEnv (volStage, volEnv, stageTime, vp.delayVol, vp.attackVol, vp.holdVol, vp.decayVol, vp.sustainVol, vp.releaseVol, dt, true);
        advanceEnv (modStage, modEnv, modTime, vp.delayMod, vp.attackMod, vp.holdMod, vp.decayMod, vp.sustainMod, vp.releaseMod, dt, false);

        const float x = std::min (1.0f, t / curveSec);
        float cents = (basePitch - vp.rootKey) * vp.scaleTuning + vp.tuneCents + (curve.eval (x, t) + bend) * 100.0f;
        cents += modEnv * vp.modEnvToPitch;
        if (vp.vibLfoToPitch != 0.0f && t > vp.vibLfoDelay) cents += vp.vibLfoToPitch * std::sin (kTwoPi * vp.vibLfoFreq * (t - vp.vibLfoDelay));
        float lfo = 0.0f;
        if (t > vp.modLfoDelay) lfo = std::sin (kTwoPi * vp.modLfoFreq * (t - vp.modLfoDelay));
        cents += lfo * vp.modLfoToPitch;
        inc = std::exp2 (cents / 1200.0) * vp.sampleRate / sr;

        if (filterOn || vp.modEnvToFc != 0.0f || vp.modLfoToFc != 0.0f)
        {
            const float fc = fcBase * std::exp2 ((modEnv * vp.modEnvToFc + lfo * vp.modLfoToFc) / 1200.0f);
            svfL.set (std::clamp (fc, 30.0f, sr * 0.45f), vp.filterQ, sr);
            filterOn = true;
        }
        if ((flags & EvSlideOutFade) && x > curve.slideOutStart)
            fade = std::max (0.0f, 1.0f - (x - curve.slideOutStart) / std::max (0.05f, 1.0f - curve.slideOutStart));
        t += dt;
    }
};

class SF2Instrument final : public Instrument
{
public:
    SF2Instrument (std::shared_ptr<const SoundFont> s, const Track& track)
        : sf (std::move (s)), family (track.family), stringed (isStringModelFamily (track.family))
    {
        bank = track.percussion ? 128 : 0;
        program = track.percussion ? 0 : std::clamp (track.program, 0, 127);
        voices.resize (96);
        harmonicsProgram = (isGuitarFamily (family) && sf->hasPreset (0, 31)) ? 31 : -1;
        mutedProgram = (isGuitarFamily (family) && sf->hasPreset (0, 28)) ? 28 : -1;
        if (const auto* p = sf->findPreset (bank, program)) presetName = p->name;
    }

    const char* engineName() const override { return "SoundFont"; }
    std::string presetName;

    void prepare (float sampleRate) override { for (auto& v : voices) v.prepare (sampleRate); }

    void noteOn (const NoteEvent& e, double secPerTick) override
    {
        const int key = std::clamp ((int) std::lround (e.pitch), 0, 127);

        if (stringed && e.string >= 0)
        {
            if (e.flags & EvLegato)
            {
                bool any = false;
                for (auto& v : voices)
                    if (v.active && v.string == e.string) { v.retune (e, secPerTick); any = true; }
                if (any) return;
            }
            for (auto& v : voices) if (v.active && v.string == e.string) v.fastRelease();
        }

        int prog = program;
        float velScale = 1.0f, fcScale = 1.0f, maxDur = 1.0e9f;
        if (e.flags & EvHarmonic) { if (harmonicsProgram >= 0) prog = harmonicsProgram; else fcScale = 0.5f; }
        if (e.flags & EvPalmMute) { maxDur = 0.22f; fcScale *= 0.3f; velScale = 0.9f; }
        if (e.flags & EvDead)
        {
            maxDur = 0.04f; fcScale *= 0.2f; velScale = 0.75f;
            if (mutedProgram >= 0) prog = mutedProgram;
        }

        SoundFont::VoiceParams params[16];
        const int vel = std::clamp ((int) std::lround (e.velocity * 127.0f), 1, 127);
        const int n = sf->resolve (bank, prog, key, vel, params, 16);
        for (int i = 0; i < n; ++i)
        {
            if (params[i].exclusiveClass != 0)
                for (auto& v : voices) if (v.active && v.exclusive == params[i].exclusiveClass) v.fastRelease();
            SFVoice* v = nullptr;
            for (auto& x : voices) if (! x.active) { v = &x; break; }
            if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; }
            v->start (params[i], e, secPerTick, ++stamp, velScale, fcScale, maxDur);
            v->setBend (bend);
        }
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
    std::shared_ptr<const SoundFont> sf;
    Family family;
    bool stringed;
    int bank = 0, program = 0, harmonicsProgram = -1, mutedProgram = -1;
    std::vector<SFVoice> voices;
    uint64_t stamp = 0;
    float bend = 0.0f;
};
} // namespace

std::unique_ptr<Instrument> makeSoundFontInstrument (std::shared_ptr<const SoundFont> sf, const Track& track)
{
    if (! sf) return nullptr;
    const int bank = track.percussion ? 128 : 0;
    const int prog = track.percussion ? 0 : track.program;
    SoundFont::VoiceParams tmp[4];
    if (sf->resolve (bank, prog, 60, 100, tmp, 4) == 0 && sf->resolve (bank, prog, 38, 100, tmp, 4) == 0) return nullptr;
    return std::make_unique<SF2Instrument> (std::move (sf), track);
}
} // namespace tc
