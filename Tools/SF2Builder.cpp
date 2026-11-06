// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// SoundFont builder: renders Tablarion's own modeled instruments into a General MIDI SoundFont 2 bank.
//
//   SF2Builder <out.sf2> [--bake-guitar-fx] [--rate 44100] [--step N]
//
// * Bank 0, programs 0-127: every GM melodic instrument (multi-sampled, looped where the sound sustains).
// * Bank 128, program 0: GM percussion kit (keys 27-87), hi-hat / triangle / whistle / guiro / cuica choke groups.
// * Programs 28 (muted guitar) and 31 (guitar harmonics) are rendered with the dead-note and harmonic
//   articulations; Tablarion's SoundFont engine switches to them for those guitar techniques.
// * Guitars and basses are rendered dry by default because Tablarion keeps its amp/cabinet chain on
//   SoundFont guitar tracks. Pass --bake-guitar-fx to bake the amp sims in (for other SF2 players).
// All audio comes from the project's own synthesis code: no third-party samples are used.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Engine/Instrument.h"
#include "../Source/Engine/TrackFX.h"
#include "../Source/Engine/SoundFont.h"
#include <atomic>
#include <thread>

namespace
{
const char* const kGMNames[128] = {
    "Acoustic Grand Piano", "Bright Piano", "Electric Grand", "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2",
    "Harpsichord", "Clavinet", "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone",
    "Tubular Bells", "Dulcimer", "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ",
    "Accordion", "Harmonica", "Tango Accordion", "Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar",
    "Muted Guitar", "Overdrive Guitar", "Distortion Guitar", "Guitar Harmonics", "Acoustic Bass", "Finger Bass",
    "Pick Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2", "Violin", "Viola",
    "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani", "String Ensemble 1",
    "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice",
    "Orchestra Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1",
    "Synth Brass 2", "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon",
    "Clarinet", "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Square Lead", "Saw Lead", "Calliope Lead", "Chiff Lead", "Charang", "Voice Lead", "Fifths Lead", "Bass + Lead",
    "New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
    "Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-fi", "Sitar", "Banjo",
    "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai", "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
    "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal", "Guitar Fret Noise", "Breath Noise", "Seashore",
    "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot" };

struct Options
{
    double rate = 44100.0;
    bool bakeGuitarFx = false;
    int step = 0; // 0 = per family default
};

struct SampleOut
{
    std::string name;
    std::vector<float> data;
    int root = 60;
    bool looped = false;
    uint32_t loopStart = 0, loopEnd = 0;
};

struct ZoneOut
{
    int sample = -1; // index into the program's samples
    int keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
    int pan = 0;     // 0.1 % units
    int exclusive = 0;
};

struct ProgramOut
{
    std::string name;
    int bank = 0, program = 0;
    std::vector<SampleOut> samples;
    std::vector<ZoneOut> zones;
    int attenuationCb = 0;
    float releaseSec = 0.2f;
};

// ------------------------------------------------------------------------------------------------
tc::Track makeTrack (int program, bool drums)
{
    tc::Track t;
    t.program = program;
    t.percussion = drums;
    t.tuning = { 40, 45, 50, 55, 59, 64 };
    if (program >= 32 && program <= 39) t.tuning = { 28, 33, 38, 43, 48 };
    if (program == 46) t.tuning = {};
    t.family = tc::classifyTrack (t);
    return t;
}

bool bakeFxFor (tc::Family f, const Options& o)
{
    return o.bakeGuitarFx || ! (tc::isGuitarFamily (f) || tc::isBassFamily (f));
}

/** Renders one note through a fresh instrument (and the family chain if requested). Returns stereo. */
void renderNote (const tc::Track& t, bool fx, float pitch, float vel, uint32_t flags, float noteSec, float totalSec,
                 double sr, std::vector<float>& L, std::vector<float>& R)
{
    auto inst = tc::makeModeledInstrument (t);
    inst->prepare ((float) sr);
    tc::TrackFX chain;
    constexpr int block = 256;
    if (fx) chain.prepare ((float) sr, block, t.family);

    const double secPerTick = 60.0 / (120.0 * tc::PPQ);
    tc::NoteEvent e;
    e.pitch = pitch;
    e.velocity = vel;
    e.durTicks = (int64_t) (noteSec / secPerTick);
    e.curveTicks = e.durTicks;
    e.flags = flags;
    e.string = -1;
    inst->noteOn (e, secPerTick);

    const int total = (int) (totalSec * sr);
    L.assign ((size_t) total, 0.0f);
    R.assign ((size_t) total, 0.0f);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        inst->render (L.data() + pos, R.data() + pos, n);
        if (fx) chain.process (L.data() + pos, R.data() + pos, n);
    }
}

double rms (const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min (b, x.size());
    if (b <= a) return 0.0;
    double s = 0.0;
    for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (b - a));
}

float peakOf (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x) p = std::max (p, std::abs (v));
    return p;
}

/** Cuts the decayed tail at -66 dB below the peak and fades out the last few milliseconds. */
void trimTail (std::vector<float>& x, double sr)
{
    const float pk = peakOf (x);
    if (pk <= 0.0f) { x.resize (std::min<size_t> (x.size(), 64)); return; }
    const float thr = pk * 0.0005f;
    const size_t win = (size_t) (0.01 * sr);
    size_t last = 0;
    for (size_t i = 0; i < x.size(); i += win)
    {
        float m = 0.0f;
        for (size_t j = i; j < std::min (x.size(), i + win); ++j) m = std::max (m, std::abs (x[j]));
        if (m > thr) last = std::min (x.size(), i + win);
    }
    const size_t fade = (size_t) (0.03 * sr);
    const size_t end = std::min (x.size(), last + fade);
    x.resize (std::max<size_t> (end, 64));
    const size_t f0 = x.size() > fade ? x.size() - fade : 0;
    for (size_t i = f0; i < x.size(); ++i) x[i] *= (float) (x.size() - i) / (float) (x.size() - f0);
}

size_t nearestRisingZero (const std::vector<float>& x, size_t at, size_t range)
{
    size_t best = at;
    size_t bestDist = range + 1;
    const size_t lo = at > range ? at - range : 1;
    for (size_t i = lo; i < std::min (x.size() - 1, at + range); ++i)
        if (x[i - 1] <= 0.0f && x[i] > 0.0f)
        {
            const size_t d = i > at ? i - at : at - i;
            if (d < bestDist) { bestDist = d; best = i; }
        }
    return best;
}

/** Makes a seamless sustain loop with a cross-faded loop end. */
void makeLoop (SampleOut& s, double sr)
{
    auto& x = s.data;
    size_t ls = nearestRisingZero (x, (size_t) (1.0 * sr), (size_t) (0.01 * sr));
    size_t le = nearestRisingZero (x, (size_t) (2.4 * sr), (size_t) (0.01 * sr));
    const size_t xf = std::min<size_t> ((size_t) (0.25 * sr), ls - 1);

    // correlated segments -> linear crossfade, uncorrelated -> equal power
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < xf; ++i)
    {
        const double a = x[le - xf + i], b = x[ls - xf + i];
        sab += a * b; saa += a * a; sbb += b * b;
    }
    const double corr = sab / std::sqrt (std::max (1e-20, saa * sbb));
    const bool linear = corr > 0.5;

    std::vector<float> out (x.begin(), x.begin() + (long) le);
    for (size_t i = 0; i < xf; ++i)
    {
        const double w = (double) i / (double) xf;
        const double gIn = linear ? w : std::sin (w * juce::MathConstants<double>::halfPi);
        const double gOut = linear ? 1.0 - w : std::cos (w * juce::MathConstants<double>::halfPi);
        out[le - xf + i] = (float) (x[le - xf + i] * gOut + x[ls - xf + i] * gIn);
    }
    for (size_t k = 0; k < 32; ++k) out.push_back (x[ls + k]); // guard points continue the loop
    s.loopStart = (uint32_t) ls;
    s.loopEnd = (uint32_t) le;
    s.looped = true;
    x = std::move (out);
}

struct Layout { int lo, hi, step; float sec; float release; };

Layout layoutFor (tc::Family f, int program)
{
    using F = tc::Family;
    switch (f)
    {
        case F::SteelGuitar: case F::NylonGuitar: case F::CleanElectric: case F::JazzElectric:
        case F::OverdriveGuitar: case F::DistortionGuitar: return { 40, 88, 3, 5.0f, 0.12f };
        case F::MutedElectric: return { 40, 88, 4, 1.5f, 0.08f };
        case F::HarmonicsGuitar: return { 52, 100, 4, 4.0f, 0.2f };
        case F::FingerBass: case F::PickBass: case F::SlapBass: case F::FretlessBass:
        case F::AcousticBass: return { 28, 67, 3, 4.0f, 0.1f };
        case F::SynthBass: return { 24, 72, 4, 3.0f, 0.1f };
        case F::Banjo: case F::Mandolin: case F::Ukulele: return { 48, 96, 4, 3.0f, 0.15f };
        case F::Sitar: return { 40, 88, 4, 5.0f, 0.3f };
        case F::Harp: return { 24, 103, 4, 5.0f, 1.2f };
        case F::Piano: return { 21, 108, 3, program <= 3 ? 6.0f : 4.0f, 0.3f };
        case F::EPiano: return { 28, 103, 4, 5.0f, 0.25f };
        case F::Organ: return { 24, 96, 6, 2.6f, 0.06f };
        case F::Mallets: return { 36, 108, 4, 4.0f, 0.8f };
        case F::Strings: return program == 43 || program == 42 ? Layout { 24, 72, 6, 2.6f, 0.35f } : Layout { 28, 100, 6, 2.6f, 0.35f };
        case F::Brass: return program == 58 ? Layout { 24, 72, 6, 2.6f, 0.15f } : Layout { 34, 88, 6, 2.6f, 0.15f };
        case F::Reed: return { 36, 90, 6, 2.6f, 0.12f };
        case F::Pipe: return { 48, 100, 6, 2.6f, 0.15f };
        case F::Choir: return { 36, 88, 6, 2.6f, 0.45f };
        case F::SynthLead: return { 36, 100, 6, 2.6f, 0.15f };
        case F::SynthPad: return { 28, 96, 6, 2.6f, 0.9f };
        default: return { 24, 100, 6, 3.0f, 0.5f };
    }
}

/** Velocity layers: families whose timbre changes strongly with touch get a soft and a hard layer. */
std::vector<std::pair<float, int>> layersFor (tc::Family f)
{
    using F = tc::Family;
    const bool twoLayers = tc::isStringModelFamily (f) || f == F::Piano || f == F::EPiano || f == F::Drums || f == F::Mallets;
    if (twoLayers && f != F::HarmonicsGuitar && f != F::MutedElectric) return { { 0.42f, 0 }, { 0.95f, 85 } };
    return { { 0.9f, 0 } };
}

uint32_t flagsFor (int program)
{
    if (program == 31) return tc::EvHarmonic;
    return 0;
}

/** Converts a level boost (linear gain applied at build time) into centibels of initialAttenuation. */
int attenuationFor (float gain) { return std::clamp ((int) std::lround (200.0 * std::log10 (std::max (1.0f, gain))), 0, 1440); }

ProgramOut buildMelodic (int program, const Options& o)
{
    ProgramOut p;
    p.name = kGMNames[program];
    p.program = program;
    const auto t = makeTrack (program, false);
    const bool fx = bakeFxFor (t.family, o);
    const auto lay = layoutFor (t.family, program);
    const auto layers = layersFor (t.family);
    const int step = o.step > 0 ? o.step : lay.step;
    p.releaseSec = lay.release;

    std::vector<int> roots;
    for (int k = lay.lo; k <= lay.hi; k += step) roots.push_back (k);

    // sustained or decaying? probe the middle of the range
    std::vector<float> L, R;
    const int probe = roots[roots.size() / 2];
    renderNote (t, fx, (float) probe, 0.9f, flagsFor (program), 10.0f, 2.6f, o.rate, L, R);
    for (size_t i = 0; i < L.size(); ++i) L[i] = 0.5f * (L[i] + R[i]);
    const double early = rms (L, (size_t) (0.9 * o.rate), (size_t) (1.1 * o.rate));
    const double late = rms (L, (size_t) (2.2 * o.rate), (size_t) (2.4 * o.rate));
    const bool sustained = early > 0.02 * peakOf (L) && late > 0.6 * early && program != 47 && program != 45;

    float rawPeakHard = 0.0f;
    std::vector<float> layerPeak (layers.size(), 0.0f);
    std::vector<std::vector<size_t>> layerSamples (layers.size());

    for (size_t li = 0; li < layers.size(); ++li)
        for (size_t ri = 0; ri < roots.size(); ++ri)
        {
            const int root = roots[ri];
            const float sec = sustained ? 2.6f : std::max (1.2f, lay.sec * (root > 84 ? 0.5f : root > 70 ? 0.75f : 1.0f));
            renderNote (t, fx, (float) root, layers[li].first, flagsFor (program), sustained ? 10.0f : sec, sec, o.rate, L, R);
            SampleOut s;
            s.root = root;
            s.data.resize (L.size());
            for (size_t i = 0; i < L.size(); ++i) s.data[i] = 0.5f * (L[i] + R[i]);
            s.name = juce::String (program).paddedLeft ('0', 3).toStdString() + "_" + juce::String (root).toStdString()
                   + (layers.size() > 1 ? (li == 0 ? "p" : "f") : "");
            layerPeak[li] = std::max (layerPeak[li], peakOf (s.data));
            if (sustained) makeLoop (s, o.rate);
            else trimTail (s.data, o.rate);
            layerSamples[li].push_back (p.samples.size());
            p.samples.push_back (std::move (s));

            ZoneOut z;
            z.sample = (int) p.samples.size() - 1;
            z.keyLo = ri == 0 ? 0 : (roots[ri - 1] + root) / 2 + 1;
            z.keyHi = ri + 1 == roots.size() ? 127 : (root + roots[ri + 1]) / 2;
            z.velLo = layers[li].second;
            z.velHi = li + 1 < layers.size() ? layers[li + 1].second - 1 : 127;
            p.zones.push_back (z);
        }
    rawPeakHard = layerPeak.back();

    // normalise each layer to -1 dBFS, keep the engine's absolute level through initialAttenuation
    const float target = 0.89f;
    for (size_t li = 0; li < layers.size(); ++li)
    {
        const float g = layerPeak[li] > 0.0f ? target / layerPeak[li] : 1.0f;
        for (auto idx : layerSamples[li]) for (auto& v : p.samples[idx].data) v *= g;
    }
    p.attenuationCb = attenuationFor (rawPeakHard > 0.0f ? target / rawPeakHard : 1.0f);
    printf ("  %3d %-22s %-18s %s %2zu samples  att %4.1f dB\n", program, p.name.c_str(), tc::familyName (t.family),
            sustained ? "loop" : "one ", p.samples.size(), p.attenuationCb / 10.0);
    fflush (stdout);
    return p;
}

int exclusiveFor (int key)
{
    switch (key)
    {
        case 42: case 44: case 46: return 1; // hi-hat
        case 80: case 81: return 2;          // triangle
        case 71: case 72: return 3;          // whistle
        case 73: case 74: return 4;          // guiro
        case 78: case 79: return 5;          // cuica
        default: return 0;
    }
}

ProgramOut buildDrums (const Options& o)
{
    ProgramOut p;
    p.name = "Standard Kit";
    p.bank = 128;
    p.program = 0;
    p.releaseSec = 6.0f; // drum hits ring out after note-off
    const auto t = makeTrack (0, true);
    const auto layers = layersFor (t.family);
    std::vector<float> L, R;
    std::vector<float> layerPeak (layers.size(), 0.0f);
    std::vector<std::vector<size_t>> layerSamples (layers.size());

    for (size_t li = 0; li < layers.size(); ++li)
        for (int key = 27; key <= 87; ++key)
        {
            const bool cymbal = key == 49 || key == 51 || key == 52 || key == 55 || key == 57 || key == 59 || key == 53;
            renderNote (t, true, (float) key, layers[li].first, 0, 0.2f, cymbal ? 5.0f : 2.5f, o.rate, L, R);
            const double rl = rms (L, 0, L.size()), rr = rms (R, 0, R.size());
            SampleOut s;
            s.root = key;
            s.data.resize (L.size());
            for (size_t i = 0; i < L.size(); ++i) s.data[i] = 0.5f * (L[i] + R[i]);
            layerPeak[li] = std::max (layerPeak[li], peakOf (s.data));
            trimTail (s.data, o.rate);
            s.name = "drum_" + juce::String (key).toStdString() + (li == 0 ? "p" : "f");
            layerSamples[li].push_back (p.samples.size());
            p.samples.push_back (std::move (s));

            ZoneOut z;
            z.sample = (int) p.samples.size() - 1;
            z.keyLo = z.keyHi = key;
            z.velLo = layers[li].second;
            z.velHi = li + 1 < layers.size() ? layers[li + 1].second - 1 : 127;
            z.pan = (rl + rr) > 1e-9 ? (int) std::lround (500.0 * (rr - rl) / (rr + rl)) : 0;
            z.exclusive = exclusiveFor (key);
            p.zones.push_back (z);
        }

    // one gain per layer keeps the kit's internal balance
    const float target = 0.89f;
    for (size_t li = 0; li < layers.size(); ++li)
    {
        const float g = layerPeak[li] > 0.0f ? target / layerPeak[li] : 1.0f;
        for (auto idx : layerSamples[li]) for (auto& v : p.samples[idx].data) v *= g;
    }
    p.attenuationCb = attenuationFor (layerPeak.back() > 0.0f ? target / layerPeak.back() : 1.0f);
    printf ("  drum kit: %zu samples  att %4.1f dB\n", p.samples.size(), p.attenuationCb / 10.0);
    return p;
}

// ------------------------------------------------------------------------------------------------
// SF2 writer

struct Bytes
{
    std::vector<uint8_t> b;
    void u8 (uint32_t v) { b.push_back ((uint8_t) v); }
    void u16 (uint32_t v) { u8 (v & 0xff); u8 ((v >> 8) & 0xff); }
    void u32 (uint32_t v) { u16 (v & 0xffff); u16 (v >> 16); }
    void name20 (const std::string& s) { for (int i = 0; i < 20; ++i) u8 (i < (int) s.size() && i < 19 ? (uint8_t) s[(size_t) i] : 0); }
    void fourcc (const char* c) { for (int i = 0; i < 4; ++i) u8 ((uint8_t) c[i]); }
    void append (const Bytes& o) { b.insert (b.end(), o.b.begin(), o.b.end()); }
    void chunk (const char* id, const Bytes& body)
    {
        fourcc (id); u32 ((uint32_t) body.b.size()); append (body);
        if (body.b.size() & 1) u8 (0);
    }
    void list (const char* type, const Bytes& body)
    {
        Bytes l; l.fourcc (type); l.append (body);
        chunk ("LIST", l);
    }
};

Bytes zstr (const std::string& s)
{
    Bytes b;
    for (char c : s) b.u8 ((uint8_t) c);
    b.u8 (0);
    if (b.b.size() & 1) b.u8 (0);
    return b;
}

enum Gen
{
    GPan = 17, GAttackVol = 34, GHoldVol = 35, GDecayVol = 36, GSustainVol = 37, GReleaseVol = 38,
    GInstrument = 41, GKeyRange = 43, GVelRange = 44, GAtten = 48, GSampleID = 53, GSampleModes = 54,
    GExclusive = 57, GRootKey = 58
};

int timecents (float sec) { return (int) std::lround (1200.0 * std::log2 (std::max (0.001f, sec))); }

bool writeSF2 (const juce::File& file, std::vector<ProgramOut>& progs, double rate, const std::string& comment)
{
    Bytes smpl, phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
    uint32_t pos = 0;
    int pbagCount = 0, pgenCount = 0, ibagCount = 0, igenCount = 0, sampleCount = 0;

    auto pgenAdd = [&] (int op, uint32_t amt) { pgen.u16 ((uint32_t) op); pgen.u16 (amt & 0xffff); ++pgenCount; };
    auto igenAdd = [&] (int op, uint32_t amt) { igen.u16 ((uint32_t) op); igen.u16 (amt & 0xffff); ++igenCount; };
    auto range = [] (int lo, int hi) { return (uint32_t) (lo & 0xff) | ((uint32_t) (hi & 0xff) << 8); };

    // presets sorted by bank/program as the spec recommends
    std::vector<size_t> order (progs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort (order.begin(), order.end(), [&] (size_t a, size_t b)
               { return std::make_pair (progs[a].bank, progs[a].program) < std::make_pair (progs[b].bank, progs[b].program); });

    for (size_t instIdx = 0; instIdx < order.size(); ++instIdx)
    {
        auto& p = progs[order[instIdx]];
        const int firstSample = sampleCount;
        for (auto& s : p.samples)
        {
            const uint32_t start = pos;
            for (float v : s.data) { smpl.u16 ((uint32_t) (int16_t) std::clamp ((int) std::lround (v * 32767.0f), -32768, 32767)); ++pos; }
            const uint32_t end = pos;
            for (int i = 0; i < 46; ++i) { smpl.u16 (0); ++pos; }
            shdr.name20 (s.name);
            shdr.u32 (start); shdr.u32 (end);
            shdr.u32 (s.looped ? start + s.loopStart : start); shdr.u32 (s.looped ? start + s.loopEnd : end - 1);
            shdr.u32 ((uint32_t) rate); shdr.u8 ((uint32_t) s.root); shdr.u8 (0); shdr.u16 (0); shdr.u16 (1);
            ++sampleCount;
        }

        // instrument
        inst.name20 (p.name); inst.u16 ((uint32_t) ibagCount);
        for (auto& z : p.zones)
        {
            const auto& s = p.samples[(size_t) z.sample];
            ibag.u16 ((uint32_t) igenCount); ibag.u16 (0); ++ibagCount;
            igenAdd (GKeyRange, range (z.keyLo, z.keyHi));
            igenAdd (GVelRange, range (z.velLo, z.velHi));
            if (z.pan != 0) igenAdd (GPan, (uint32_t) (int16_t) z.pan);
            if (p.attenuationCb > 0) igenAdd (GAtten, (uint32_t) p.attenuationCb);
            igenAdd (GReleaseVol, (uint32_t) (int16_t) timecents (p.releaseSec));
            if (z.exclusive) igenAdd (GExclusive, (uint32_t) z.exclusive);
            igenAdd (GRootKey, (uint32_t) s.root);
            if (s.looped) igenAdd (GSampleModes, 1);
            igenAdd (GSampleID, (uint32_t) (firstSample + z.sample));
        }

        // preset with one zone pointing at the instrument
        phdr.name20 (p.name); phdr.u16 ((uint32_t) p.program); phdr.u16 ((uint32_t) p.bank); phdr.u16 ((uint32_t) pbagCount);
        phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
        pbag.u16 ((uint32_t) pgenCount); pbag.u16 (0); ++pbagCount;
        pgenAdd (GInstrument, (uint32_t) instIdx);
    }

    // terminal records
    phdr.name20 ("EOP"); phdr.u16 (0); phdr.u16 (0); phdr.u16 ((uint32_t) pbagCount); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    pbag.u16 ((uint32_t) pgenCount); pbag.u16 (0);
    for (int i = 0; i < 10; ++i) pmod.u8 (0);
    pgen.u32 (0);
    inst.name20 ("EOI"); inst.u16 ((uint32_t) ibagCount);
    ibag.u16 ((uint32_t) igenCount); ibag.u16 (0);
    for (int i = 0; i < 10; ++i) imod.u8 (0);
    igen.u32 (0);
    shdr.name20 ("EOS"); for (int i = 0; i < 26; ++i) shdr.u8 (0);

    Bytes info;
    { Bytes v; v.u16 (2); v.u16 (1); info.chunk ("ifil", v); }
    info.chunk ("isng", zstr ("EMU8000"));
    info.chunk ("INAM", zstr ("Tablarion GM"));
    info.chunk ("ICRD", zstr ("2026"));
    info.chunk ("ICOP", zstr ("Copyright (C) 2026 the Tablarion contributors. AGPL-3.0-or-later."));
    info.chunk ("ICMT", zstr (comment));
    info.chunk ("ISFT", zstr ("Tablarion SF2Builder"));

    Bytes sdta; sdta.chunk ("smpl", smpl);
    Bytes pdta;
    pdta.chunk ("phdr", phdr); pdta.chunk ("pbag", pbag); pdta.chunk ("pmod", pmod); pdta.chunk ("pgen", pgen);
    pdta.chunk ("inst", inst); pdta.chunk ("ibag", ibag); pdta.chunk ("imod", imod); pdta.chunk ("igen", igen);
    pdta.chunk ("shdr", shdr);

    Bytes body; body.fourcc ("sfbk");
    body.list ("INFO", info); body.list ("sdta", sdta); body.list ("pdta", pdta);
    Bytes riff; riff.chunk ("RIFF", body);

    file.deleteFile();
    return file.replaceWithData (riff.b.data(), riff.b.size());
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { printf ("usage: SF2Builder <out.sf2> [--bake-guitar-fx] [--rate 44100] [--step N]\n"); return 1; }
    Options o;
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--bake-guitar-fx") o.bakeGuitarFx = true;
        else if (a == "--rate" && i + 1 < argc) o.rate = atof (argv[++i]);
        else if (a == "--step" && i + 1 < argc) o.step = atoi (argv[++i]);
    }

    std::vector<ProgramOut> progs (129);
    std::atomic<int> next { 0 };
    const int nThreads = std::max (1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < nThreads; ++i)
        pool.emplace_back ([&]
        {
            for (int k; (k = next++) < 129;)
                progs[(size_t) k] = k == 128 ? buildDrums (o) : buildMelodic (k, o);
        });
    for (auto& th : pool) th.join();

    size_t frames = 0;
    for (auto& p : progs) for (auto& s : p.samples) frames += s.data.size();
    const std::string comment = std::string ("General MIDI bank rendered from Tablarion's modeled instruments. ")
        + (o.bakeGuitarFx ? "Guitar and bass amp sims are baked in (for any SF2 player)."
                          : "Guitars and basses are dry: Tablarion adds its amp/cabinet chain.")
        + " Programs 28 and 31 double as Tablarion's dead-note and harmonic articulation layers.";
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])));
    if (! writeSF2 (out, progs, o.rate, comment)) { printf ("write failed: %s\n", out.getFullPathName().toRawUTF8()); return 1; }
    printf ("wrote %s: %.1f MB, %.1f min of audio, %.1f s\n", out.getFullPathName().toRawUTF8(), out.getSize() / 1048576.0,
            frames / o.rate / 60.0, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);

    // verify with the engine's own loader
    juce::String err;
    auto sf = tc::SoundFont::load (out, err);
    if (! sf) { printf ("verify: load failed: %s\n", err.toRawUTF8()); return 1; }
    int missing = 0;
    tc::SoundFont::VoiceParams vp[8];
    for (int prog = 0; prog < 128; ++prog)
        if (! sf->hasPreset (0, prog) || sf->resolve (0, prog, 60, 100, vp, 8) == 0) { printf ("verify: program %d unresolved\n", prog); ++missing; }
    for (int key = 27; key <= 87; ++key)
        if (sf->resolve (128, 0, key, 100, vp, 8) == 0) { printf ("verify: drum key %d unresolved\n", key); ++missing; }
    printf ("verify: %zu presets, %zu instruments, %zu samples, %d problems\n", sf->presets.size(), sf->instruments.size(), sf->samples.size(), missing);
    return missing == 0 ? 0 : 1;
}
