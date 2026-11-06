// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — compiled performance events
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "../Model/Score.h"

namespace tc
{
enum EventFlags : uint32_t
{
    EvPalmMute   = 1u << 0,
    EvDead       = 1u << 1,
    EvGhost      = 1u << 2,
    EvLegato     = 1u << 3,  // do not re-pluck: continue the previous note on the same string
    EvHarmonic   = 1u << 4,
    EvTapped     = 1u << 5,
    EvSlap       = 1u << 6,
    EvPop        = 1u << 7,
    EvLetRing    = 1u << 8,
    EvStaccato   = 1u << 9,
    EvAccent     = 1u << 10,
    EvPinch      = 1u << 11,
    EvHammerOn   = 1u << 12,
    EvPullOff    = 1u << 13,
    EvUpStroke   = 1u << 14,
    EvSlideOutFade = 1u << 15,
    EvLive       = 1u << 16  // live MIDI note: sustains until note-off
};

/** Continuous pitch modulation applied over a note (bends, whammy, vibrato, slides). */
struct PitchCurve
{
    static constexpr int kMaxPoints = 10;
    BendPoint pts[kMaxPoints];
    int   numPts = 0;
    float vibratoDepth = 0.0f;  // semitones (peak)
    float vibratoRate  = 5.5f;  // Hz
    float slideOutSemis = 0.0f; // glide amount reached at the end of the curve span
    float slideOutStart = 0.7f; // fraction of the span where the glide starts
    float slideInSemis  = 0.0f; // starting offset that glides to 0
    float slideInFrac   = 0.12f;

    void addPoint (float pos, float semis)
    {
        if (numPts < kMaxPoints) pts[numPts++] = { pos, semis };
    }

    bool isFlat() const { return numPts == 0 && vibratoDepth == 0.0f && slideOutSemis == 0.0f && slideInSemis == 0.0f; }

    /** x = 0..1 position within the span, tSec = seconds since note start. */
    float eval (float x, float tSec) const
    {
        float v = 0.0f;
        if (numPts > 0)
        {
            if (x <= pts[0].pos) v = pts[0].semis;
            else if (x >= pts[numPts - 1].pos) v = pts[numPts - 1].semis;
            else
                for (int i = 1; i < numPts; ++i)
                    if (x <= pts[i].pos)
                    {
                        const float span = std::max (1.0e-4f, pts[i].pos - pts[i - 1].pos);
                        float u = (x - pts[i - 1].pos) / span;
                        u = u * u * (3.0f - 2.0f * u); // smooth like a real finger bend
                        v = pts[i - 1].semis + (pts[i].semis - pts[i - 1].semis) * u;
                        break;
                    }
        }
        if (slideInSemis != 0.0f && x < slideInFrac)
        {
            const float u = x / slideInFrac;
            v += slideInSemis * (1.0f - u);
        }
        if (slideOutSemis != 0.0f && x > slideOutStart)
        {
            const float u = std::min (1.0f, (x - slideOutStart) / std::max (1.0e-3f, 1.0f - slideOutStart));
            v += slideOutSemis * u;
        }
        if (vibratoDepth != 0.0f)
        {
            const float fadeIn = std::min (1.0f, tSec * 4.0f);
            v += vibratoDepth * fadeIn * std::sin (6.2831853f * vibratoRate * tSec);
        }
        return v;
    }
};

struct NoteEvent
{
    int64_t tick = 0;
    int64_t durTicks = 0;   // sounding duration
    int64_t curveTicks = 0; // span over which the pitch curve is defined (written duration)
    int     track = 0;
    int     string = -1;    // 0 = lowest
    float   pitch = 60.0f;  // MIDI note (fractional allowed)
    float   velocity = 0.7f;
    uint32_t flags = 0;
    PitchCurve curve;
};

struct TempoMap
{
    struct Seg { int64_t tick; double bpm; };
    std::vector<Seg> segs { { 0, 120.0 } };

    double bpmAt (int64_t tick) const
    {
        auto it = std::upper_bound (segs.begin(), segs.end(), tick, [] (int64_t t, const Seg& s) { return t < s.tick; });
        if (it == segs.begin()) return segs.front().bpm;
        return std::prev (it)->bpm;
    }

    double secondsAt (int64_t tick) const
    {
        double sec = 0.0;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const int64_t start = segs[i].tick;
            if (tick <= start) break;
            const int64_t end = (i + 1 < segs.size()) ? std::min (tick, segs[i + 1].tick) : tick;
            sec += (double) (end - start) / PPQ * 60.0 / segs[i].bpm;
        }
        return sec;
    }
};

struct BarMark
{
    int64_t tick;
    int     masterBar;
};

/** A score rendered to a flat, time-sorted event list ready for real-time playback. */
struct CompiledSong
{
    std::vector<NoteEvent> events; // sorted by tick
    TempoMap tempo;
    std::vector<BarMark> bars;     // play order
    int64_t lengthTicks = 0;
    double  lengthSeconds = 0.0;
};

struct CompileOptions
{
    float humanize = 0.15f; // 0..1
    bool  autoStrum = true; // natural strum spread on guitar chords
    int   seed = 1234;
};

std::unique_ptr<CompiledSong> compileScore (const Score& score, const CompileOptions& opts);
} // namespace tc
