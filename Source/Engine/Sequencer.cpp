// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — the "conductor": interprets a Guitar Pro score into a performance.
//
// Like a real player, the interpreter resolves notation into gestures: repeats and alternate endings,
// ties, let-ring, hammer-ons/pull-offs, slides, bends, whammy, vibrato, harmonics, trills, tremolo
// picking, grace notes, brush/arpeggio strums, natural chord strumming, swing feel and humanisation.
#include "Events.h"

#include <map>
#include <random>

namespace tc
{
namespace
{
std::vector<int> buildPlayOrder (const std::vector<MasterBar>& mbs)
{
    std::vector<int> order;
    const int n = (int) mbs.size();
    int i = 0, start = 0, pass = 1;
    const size_t guard = (size_t) n * 8 + 1000;

    while (i < n && order.size() < guard)
    {
        const auto& mb = mbs[(size_t) i];
        if (mb.repeatStart && i != start) { start = i; pass = 1; }

        if (mb.alternateEndings != 0 && pass <= 32 && (mb.alternateEndings & (1u << (pass - 1))) == 0)
        {
            ++i;
            continue;
        }

        order.push_back (i);

        if (mb.repeatCount > 0)
        {
            if (pass < mb.repeatCount) { ++pass; i = start; continue; }
            pass = 1;
            start = i + 1;
        }
        ++i;
    }
    return order;
}

int64_t swingRemap (int64_t posInBar, TripletFeel feel)
{
    if (feel == TripletFeel::None) return posInBar;
    const int64_t unit = feel == TripletFeel::Eighth ? PPQ : PPQ / 2; // swung pair length
    const int64_t half = unit / 2, swungHalf = unit * 2 / 3;
    const int64_t base = (posInBar / unit) * unit;
    const int64_t x = posInBar - base;
    if (x <= half) return base + x * swungHalf / half;
    return base + swungHalf + (x - half) * (unit - swungHalf) / (unit - half);
}

/** Pitch of a harmonic touched at `fretPos` frets above the base. */
float harmonicOffset (float fretPos)
{
    if (fretPos <= 0.0f) return 12.0f;
    const double frac = 1.0 - std::pow (2.0, -fretPos / 12.0); // position along the string
    double best = 1e9; int bestN = 2;
    for (int n = 2; n <= 8; ++n)
        for (int k = 1; k < n; ++k)
        {
            const double d = std::abs (frac - (double) k / n);
            if (d < best - 1e-6) { best = d; bestN = n; }
        }
    return (float) (12.0 * std::log2 ((double) bestN));
}

struct Raw
{
    int64_t tick;
    int64_t dur;
    const Note* note;
    const Beat* beat;
    float velocity;
    int beatKey; // groups notes struck together
};

class Rng
{
public:
    explicit Rng (int seed) : gen ((uint32_t) seed) {}
    float uni() { return std::uniform_real_distribution<float> (-1.0f, 1.0f) (gen); }
    float gauss() { return std::normal_distribution<float> (0.0f, 0.5f) (gen); }
private:
    std::mt19937 gen;
};

class Compiler
{
public:
    Compiler (const Score& s, const CompileOptions& o) : score (s), opts (o), rng (o.seed) {}

    std::unique_ptr<CompiledSong> run()
    {
        out = std::make_unique<CompiledSong>();
        order = buildPlayOrder (score.masterBars);
        if (order.empty()) order.push_back (0);

        int64_t t = 0;
        for (int m : order)
        {
            barStarts.push_back (t);
            out->bars.push_back ({ t, m });
            t += score.masterBars[(size_t) m].lengthTicks();
        }
        out->lengthTicks = t;

        buildTempoMap();

        for (size_t ti = 0; ti < score.tracks.size(); ++ti)
            compileTrack ((int) ti);

        std::stable_sort (out->events.begin(), out->events.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });
        out->lengthSeconds = out->tempo.secondsAt (out->lengthTicks);
        return std::move (out);
    }

private:
    const Score& score;
    CompileOptions opts;
    Rng rng;
    std::unique_ptr<CompiledSong> out;
    std::vector<int> order;
    std::vector<int64_t> barStarts;

    int64_t msToTicks (double ms, int64_t atTick) const
    {
        return (int64_t) std::llround (ms / 1000.0 * out->tempo.bpmAt (atTick) / 60.0 * PPQ);
    }

    void buildTempoMap()
    {
        std::vector<TempoMap::Seg> segs { { 0, score.tempo } };
        for (size_t k = 0; k < order.size(); ++k)
        {
            const int m = order[k];
            const int len = score.masterBars[(size_t) m].lengthTicks();
            for (auto& tp : score.tempos)
                if (tp.bar == m && tp.bpm > 10.0 && tp.bpm < 1000.0)
                    segs.push_back ({ barStarts[k] + (int64_t) (tp.pos * len), tp.bpm });
        }
        std::stable_sort (segs.begin(), segs.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
        std::vector<TempoMap::Seg> clean;
        for (auto& s : segs)
        {
            if (! clean.empty() && clean.back().tick == s.tick) clean.back() = s;
            else if (clean.empty() || clean.back().bpm != s.bpm) clean.push_back (s);
        }
        out->tempo.segs = clean;
    }

    // ------------------------------------------------------------------------------------------
    std::vector<Raw> collect (const Track& track)
    {
        std::vector<Raw> raws;
        int beatKey = 0;

        for (size_t k = 0; k < order.size(); ++k)
        {
            const int m = order[k];
            const auto& mb = score.masterBars[(size_t) m];
            if ((size_t) m >= track.bars.size()) continue;
            const Bar& bar = track.bars[(size_t) m];
            const int64_t barStart = barStarts[k];
            const int64_t barLen = mb.lengthTicks();

            for (auto& voice : bar.voices)
            {
                int64_t pos = 0;
                std::vector<const Beat*> graces;
                for (auto& beat : voice.beats)
                {
                    if (beat.grace != GraceType::None) { graces.push_back (&beat); continue; }
                    if (pos >= barLen + PPQ * 4) break; // overfull bar safety

                    int64_t on  = swingRemap (pos, mb.triplet);
                    int64_t end = swingRemap (pos + beat.duration, mb.triplet);
                    int64_t onset = barStart + on;
                    int64_t dur = std::max<int64_t> (1, end - on);

                    if (! graces.empty())
                    {
                        const int64_t g = std::clamp<int64_t> (msToTicks (55.0, onset), 20, std::max<int64_t> (20, dur / 2));
                        const int ng = (int) graces.size();
                        for (int gi = 0; gi < ng; ++gi)
                        {
                            const bool before = graces[(size_t) gi]->grace == GraceType::BeforeBeat;
                            const int64_t gt = before ? onset - (ng - gi) * g : onset + gi * g;
                            ++beatKey;
                            for (auto& n : graces[(size_t) gi]->notes)
                                raws.push_back ({ std::max<int64_t> (0, gt), g, &n, graces[(size_t) gi], velocityOf (n, *graces[(size_t) gi]) * 0.85f, beatKey });
                        }
                        if (graces.front()->grace == GraceType::OnBeat)
                        {
                            const int64_t shift = ng * g;
                            onset += shift;
                            dur = std::max<int64_t> (g, dur - shift);
                        }
                        graces.clear();
                    }

                    ++beatKey;
                    if (! beat.rest)
                        for (auto& n : beat.notes)
                            raws.push_back ({ onset, dur, &n, &beat, velocityOf (n, beat), beatKey });

                    pos += beat.duration;
                }
            }
        }

        std::stable_sort (raws.begin(), raws.end(), [] (const Raw& a, const Raw& b)
        {
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.note->string < b.note->string;
        });
        return raws;
    }

    static float velocityOf (const Note& n, const Beat& b)
    {
        float v = b.dynamic >= 0.0f ? b.dynamic : n.velocity;
        if (n.ghost) v *= 0.55f;
        if (n.accent) v *= 1.18f;
        if (n.heavyAccent) v *= 1.32f;
        return std::clamp (v, 0.04f, 1.0f);
    }

    // ------------------------------------------------------------------------------------------
    void compileTrack (int ti)
    {
        const Track& track = score.tracks[(size_t) ti];
        auto raws = collect (track);
        if (raws.empty()) return;

        std::vector<NoteEvent> evs;
        evs.reserve (raws.size() + 64);

        if (track.percussion)
        {
            for (auto& r : raws)
            {
                if (r.note->tieDest) continue;
                NoteEvent e;
                e.tick = r.tick; e.durTicks = r.dur; e.curveTicks = r.dur;
                e.track = ti; e.pitch = (float) r.note->midi; e.velocity = r.velocity;
                if (r.note->ghost) e.flags |= EvGhost;
                if (r.beat->tremoloTicks > 0) addTremolo (evs, e, r.beat->tremoloTicks, false);
                else evs.push_back (e);
            }
            humanise (evs, true);
            append (evs);
            return;
        }

        const bool stringed = ! track.tuning.empty() && isStringModelFamily (track.family);
        const bool guitar = isGuitarFamily (track.family) || track.family == Family::Banjo
                            || track.family == Family::Mandolin || track.family == Family::Ukulele;

        struct StringState { int ev = -1; const Note* last = nullptr; int64_t writtenEnd = 0; };
        std::map<int, StringState> state; // key: string (stringed) or pitch (others)

        for (auto& r : raws)
        {
            const Note& n = *r.note;
            const int key = stringed && n.string >= 0 ? n.string : 1000 + n.midi;
            auto& st = state[key];

            if (n.tieDest)
            {
                if (st.ev >= 0)
                {
                    auto& e = evs[(size_t) st.ev];
                    const int64_t newEnd = r.tick + r.dur;
                    const int64_t oldSpan = std::max<int64_t> (1, e.curveTicks);
                    const int64_t newSpan = std::max<int64_t> (1, newEnd - e.tick);
                    if (! n.bend.empty())
                    {
                        // rescale existing points, then append the tied note's bend
                        const float scale = (float) oldSpan / (float) newSpan;
                        for (int i = 0; i < e.curve.numPts; ++i) e.curve.pts[i].pos *= scale;
                        if (e.curve.numPts == 0) e.curve.addPoint (0.0f, 0.0f);
                        for (auto& bp : n.bend)
                            e.curve.addPoint (scale + bp.pos * (1.0f - scale), bp.semis);
                    }
                    else if (e.curve.numPts > 0)
                    {
                        const float scale = (float) oldSpan / (float) newSpan;
                        const float hold = e.curve.pts[e.curve.numPts - 1].semis;
                        for (int i = 0; i < e.curve.numPts; ++i) e.curve.pts[i].pos *= scale;
                        e.curve.addPoint (1.0f, hold);
                    }
                    if (n.vibrato > 0) e.curve.vibratoDepth = std::max (e.curve.vibratoDepth, n.vibrato == 2 ? 0.5f : 0.22f);
                    e.curveTicks = newSpan;
                    e.durTicks = std::max (e.durTicks, newEnd - e.tick);
                    if (n.letRing) e.flags |= EvLetRing;
                    st.last = &n;
                    st.writtenEnd = newEnd;
                }
                continue;
            }

            NoteEvent e;
            e.tick = r.tick;
            e.durTicks = r.dur;
            e.curveTicks = r.dur;
            e.track = ti;
            e.string = stringed ? n.string : -1;
            e.pitch = (float) n.midi;
            e.velocity = r.velocity;

            if (n.palmMute) e.flags |= EvPalmMute;
            if (n.dead) e.flags |= EvDead;
            if (n.ghost) e.flags |= EvGhost;
            if (n.accent || n.heavyAccent) e.flags |= EvAccent;
            if (n.letRing) e.flags |= EvLetRing;
            if (n.tapped || r.beat->tap) e.flags |= EvTapped;
            if (r.beat->slap) e.flags |= EvSlap;
            if (r.beat->pop) e.flags |= EvPop;
            if (r.beat->pickStroke == Stroke::Up) e.flags |= EvUpStroke;
            if (n.staccato) { e.flags |= EvStaccato; e.durTicks = std::max<int64_t> (1, e.durTicks / 2); }

            // Harmonics
            if (n.harmonic != Harmonic::None && stringed && n.string >= 0 && n.string < (int) track.tuning.size())
            {
                const float open = (float) (track.tuning[(size_t) n.string] + track.capo);
                e.flags |= EvHarmonic;
                switch (n.harmonic)
                {
                    case Harmonic::Natural:
                        e.pitch = open + harmonicOffset (n.harmonicFret > 0 ? n.harmonicFret : (float) n.fret);
                        break;
                    case Harmonic::Pinch:
                        e.pitch = (float) n.midi + (n.harmonicFret > 0 ? harmonicOffset (n.harmonicFret) : 19.0f);
                        e.flags |= EvPinch;
                        break;
                    case Harmonic::Semi:
                        e.flags &= ~EvHarmonic;
                        e.flags |= EvPinch;
                        break;
                    case Harmonic::Feedback:
                        e.pitch = (float) n.midi + 12.0f;
                        break;
                    default: // artificial / tapped
                        e.pitch = (float) n.midi + harmonicOffset (n.harmonicFret > 0 ? n.harmonicFret : 12.0f);
                        break;
                }
            }

            // Incoming articulation from the previous note on this string
            if (st.ev >= 0 && st.last != nullptr && stringed)
            {
                auto& prev = evs[(size_t) st.ev];
                const int64_t gap = r.tick - st.writtenEnd;
                const bool close = gap <= PPQ / 4 && r.tick > prev.tick;
                if (close)
                {
                    const float delta = e.pitch - prev.pitch;
                    if (st.last->hopoOrigin || n.hopoDest)
                    {
                        e.flags |= EvLegato | (delta >= 0.0f ? EvHammerOn : EvPullOff);
                        prev.durTicks = r.tick - prev.tick + 1;
                    }
                    if (st.last->slide & (SlideLegato | SlideShift))
                    {
                        const int64_t span = std::max<int64_t> (1, r.tick - prev.tick);
                        // keep the bend/vibrato timing but end the glide exactly at the new onset
                        const float scale = (float) prev.curveTicks / (float) span;
                        for (int i = 0; i < prev.curve.numPts; ++i) prev.curve.pts[i].pos = std::min (1.0f, prev.curve.pts[i].pos * scale);
                        prev.curveTicks = span;
                        prev.curve.slideOutSemis = delta;
                        const int64_t glide = std::min<int64_t> (span * 2 / 5, std::max<int64_t> (msToTicks (90.0, prev.tick), span / 6));
                        prev.curve.slideOutStart = 1.0f - (float) glide / (float) span;
                        prev.durTicks = span + 1;
                        if (st.last->slide & SlideLegato) e.flags |= EvLegato;
                        else e.velocity *= 0.85f;
                    }
                }
            }

            if (n.slide & SlideInBelow) { e.curve.slideInSemis = -4.0f; e.curve.slideInFrac = 0.15f; }
            if (n.slide & SlideInAbove) { e.curve.slideInSemis = 4.0f; e.curve.slideInFrac = 0.15f; }
            if (n.slide & (SlideOutDown | SlideOutUp))
            {
                e.curve.slideOutSemis = (n.slide & SlideOutDown) ? -9.0f : 9.0f;
                e.curve.slideOutStart = 0.55f;
                e.flags |= EvSlideOutFade;
            }

            const auto& bend = ! n.bend.empty() ? n.bend : r.beat->whammy;
            for (auto& bp : bend) e.curve.addPoint (bp.pos, bp.semis);
            if (n.vibrato > 0) e.curve.vibratoDepth = n.vibrato == 2 ? 0.5f : 0.22f;
            if (! guitar && n.vibrato == 0 && (track.family == Family::Strings || track.family == Family::Reed || track.family == Family::Choir))
                e.curve.vibratoDepth = 0.0f;

            if (n.trillMidi >= 0 && n.trillMidi != n.midi)
            {
                addTrill (evs, e, (float) n.trillMidi, n.trillTicks > 0 ? n.trillTicks : PPQ / 8);
            }
            else if (r.beat->tremoloTicks > 0)
            {
                addTremolo (evs, e, r.beat->tremoloTicks, true);
            }
            else
                evs.push_back (e);

            st.ev = (int) evs.size() - 1;
            st.last = &n;
            st.writtenEnd = r.tick + r.dur;
        }

        if (stringed)
        {
            applyStrums (evs, raws, track, guitar);
            applyLetRing (evs);
        }
        humanise (evs, false);
        append (evs);
    }

    void addTremolo (std::vector<NoteEvent>& evs, const NoteEvent& e, int step, bool alternate)
    {
        const int64_t end = e.tick + e.curveTicks;
        int i = 0;
        for (int64_t t = e.tick; t < end; t += step, ++i)
        {
            NoteEvent x = e;
            x.tick = t;
            x.durTicks = std::min<int64_t> (step, end - t);
            x.curveTicks = x.durTicks;
            x.curve = PitchCurve();
            if (alternate && (i & 1)) x.flags |= EvUpStroke;
            x.velocity = e.velocity * (i == 0 ? 1.0f : 0.88f);
            if (i > 0) x.flags &= ~EvLegato;
            evs.push_back (x);
        }
    }

    void addTrill (std::vector<NoteEvent>& evs, const NoteEvent& e, float other, int step)
    {
        const int64_t end = e.tick + e.curveTicks;
        int i = 0;
        for (int64_t t = e.tick; t < end; t += step, ++i)
        {
            NoteEvent x = e;
            x.tick = t;
            x.durTicks = std::min<int64_t> (step, end - t) + 1;
            x.curveTicks = x.durTicks;
            x.curve = PitchCurve();
            x.pitch = (i & 1) ? other : e.pitch;
            if (i > 0)
            {
                x.flags |= EvLegato | ((i & 1) ? EvHammerOn : EvPullOff);
                x.velocity = e.velocity * 0.8f;
            }
            evs.push_back (x);
        }
    }

    void applyStrums (std::vector<NoteEvent>& evs, const std::vector<Raw>& raws, const Track& track, bool guitar)
    {
        (void) track;
        // Group events by onset tick; use the beat of the first raw note at that tick.
        std::map<int64_t, const Beat*> beatAt;
        for (auto& r : raws) beatAt.emplace (r.tick, r.beat);

        size_t i = 0;
        while (i < evs.size())
        {
            size_t j = i + 1;
            while (j < evs.size() && evs[j].tick == evs[i].tick) ++j;
            const size_t count = j - i;
            if (count >= 2)
            {
                auto it = beatAt.find (evs[i].tick);
                const Beat* beat = it != beatAt.end() ? it->second : nullptr;
                Stroke dir = Stroke::None;
                int64_t total = 0;
                if (beat && beat->brush != Stroke::None)
                {
                    dir = beat->brush;
                    total = std::max (beat->brushTicks, 20);
                    if (beat->rasgueado) total = std::max<int64_t> (total, PPQ / 4);
                }
                else if (beat && beat->arpeggio != Stroke::None)
                {
                    dir = beat->arpeggio;
                    total = std::min<int64_t> (beat->duration * 3 / 4, (int64_t) count * PPQ / 6);
                }
                else if (beat && beat->rasgueado)
                {
                    dir = Stroke::Down;
                    total = PPQ / 4;
                }
                else if (guitar && opts.autoStrum && count >= 3)
                {
                    const bool legatoGroup = (evs[i].flags & EvLegato) != 0;
                    if (! legatoGroup)
                    {
                        dir = (beat && beat->pickStroke == Stroke::Up) ? Stroke::Up : Stroke::Down;
                        if (beat && beat->pickStroke == Stroke::None)
                        {
                            // alternate strumming: off-beat eighths/sixteenths are up-strokes
                            const int64_t inQuarter = evs[i].tick % PPQ;
                            if (inQuarter != 0 && inQuarter % (PPQ / 4) == 0 && (inQuarter / (PPQ / 4)) % 2 == 1) dir = Stroke::Up;
                            else if (inQuarter == PPQ / 2) dir = Stroke::Up;
                        }
                        const double perString = 4.5 + 3.0 * (1.0 - evs[i].velocity);
                        total = msToTicks (perString * (double) (count - 1), evs[i].tick);
                    }
                }

                if (dir != Stroke::None && total > 0)
                {
                    std::vector<size_t> idx;
                    for (size_t k = i; k < j; ++k) idx.push_back (k);
                    std::sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return evs[a].string < evs[b].string; });
                    if (dir == Stroke::Up) std::reverse (idx.begin(), idx.end());
                    for (size_t k = 0; k < idx.size(); ++k)
                    {
                        auto& e = evs[idx[k]];
                        const int64_t off = total * (int64_t) k / (int64_t) std::max<size_t> (1, idx.size() - 1);
                        e.tick += off;
                        e.durTicks = std::max<int64_t> (e.durTicks - off, PPQ / 16);
                        if (dir == Stroke::Up) { e.flags |= EvUpStroke; e.velocity *= 0.92f; }
                    }
                }
            }
            i = j;
        }
        std::stable_sort (evs.begin(), evs.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });
    }

    void applyLetRing (std::vector<NoteEvent>& evs)
    {
        std::map<int, size_t> lastOn;
        std::vector<int64_t> nextOnString (evs.size(), -1);
        for (size_t k = evs.size(); k-- > 0;)
        {
            auto it = lastOn.find (evs[k].string);
            if (it != lastOn.end()) nextOnString[k] = evs[it->second].tick;
            lastOn[evs[k].string] = k;
        }
        for (size_t k = 0; k < evs.size(); ++k)
        {
            auto& e = evs[k];
            if ((e.flags & EvLetRing) == 0) continue;
            const int64_t limit = e.tick + PPQ * 16;
            const int64_t until = nextOnString[k] > e.tick ? std::min (nextOnString[k], limit) : std::min (limit, out->lengthTicks + PPQ * 4);
            e.durTicks = std::max (e.durTicks, until - e.tick);
        }
    }

    void humanise (std::vector<NoteEvent>& evs, bool drums)
    {
        const float h = std::clamp (opts.humanize, 0.0f, 1.0f);
        if (h <= 0.0f) return;
        for (auto& e : evs)
        {
            if ((e.flags & EvLegato) == 0)
            {
                const double ms = rng.gauss() * (drums ? 5.0 : 7.0) * h;
                e.tick = std::max<int64_t> (0, e.tick + msToTicks (ms, e.tick));
            }
            e.velocity = std::clamp (e.velocity * (1.0f + rng.uni() * 0.10f * h), 0.03f, 1.0f);
        }
    }

    void append (std::vector<NoteEvent>& evs)
    {
        out->events.insert (out->events.end(), evs.begin(), evs.end());
    }
};
} // namespace

std::unique_ptr<CompiledSong> compileScore (const Score& score, const CompileOptions& opts)
{
    Compiler c (score, opts);
    return c.run();
}
} // namespace tc
