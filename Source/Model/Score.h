// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — Guitar Pro score model
// A format-neutral representation of a Guitar Pro score (GP3/4/5, GPX, GP7/8).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tc
{
constexpr int PPQ = 960; // ticks per quarter note

struct BendPoint
{
    float pos   = 0.0f; // 0..1, fraction of the note's written duration
    float semis = 0.0f; // pitch offset in semitones
};

enum class Harmonic : uint8_t { None, Natural, Artificial, Pinch, Tapped, Semi, Feedback };

enum SlideFlags : uint8_t
{
    SlideShift   = 1,
    SlideLegato  = 2,
    SlideOutDown = 4,
    SlideOutUp   = 8,
    SlideInBelow = 16,
    SlideInAbove = 32
};

struct Note
{
    int   string   = -1; // 0 = lowest string, -1 = not stringed
    int   fret     = 0;
    int   midi     = 60; // sounding pitch, capo included
    float velocity = 0.72f;

    bool tieOrigin = false, tieDest = false;
    bool letRing = false, palmMute = false, dead = false, ghost = false;
    bool accent = false, heavyAccent = false, staccato = false, tenuto = false;
    bool hopoOrigin = false, hopoDest = false;
    bool tapped = false;

    int      vibrato      = 0; // 0 none, 1 slight, 2 wide
    Harmonic harmonic     = Harmonic::None;
    float    harmonicFret = 0.0f;
    uint8_t  slide        = 0; // SlideFlags
    std::vector<BendPoint> bend;
    int trillMidi  = -1;
    int trillTicks = 0;
};

enum class GraceType : uint8_t { None, BeforeBeat, OnBeat };
enum class Stroke : uint8_t { None, Down, Up };

struct Beat
{
    int  duration = PPQ;
    bool rest     = true;
    std::vector<Note> notes;

    float     dynamic    = -1.0f; // velocity 0..1, < 0 = keep note velocity
    Stroke    brush      = Stroke::None;
    int       brushTicks = 0;
    Stroke    arpeggio   = Stroke::None;
    bool      rasgueado  = false;
    Stroke    pickStroke = Stroke::None;
    int       tremoloTicks = 0;
    GraceType grace      = GraceType::None;
    std::vector<BendPoint> whammy;
    bool slap = false, pop = false, tap = false;
    bool fadeIn = false;
    double tempo = -1.0; // inline tempo change (GP3-5 mix table)
};

struct Voice { std::vector<Beat> beats; };
struct Bar   { std::vector<Voice> voices; };

enum class TripletFeel : uint8_t { None, Eighth, Sixteenth };

struct MasterBar
{
    int  num = 4, den = 4;
    bool repeatStart = false;
    int  repeatCount = 0;          // > 0: this bar closes a repeat, played repeatCount times in total
    uint32_t alternateEndings = 0; // bit i = ending i+1
    TripletFeel triplet = TripletFeel::None;
    std::string marker;

    int lengthTicks() const { return den > 0 ? num * PPQ * 4 / den : PPQ * 4; }
};

struct TempoPoint
{
    int    bar = 0;
    float  pos = 0.0f; // fraction of the bar
    double bpm = 120.0;
};

enum class Family : uint8_t
{
    SteelGuitar, NylonGuitar, CleanElectric, JazzElectric, MutedElectric,
    OverdriveGuitar, DistortionGuitar, HarmonicsGuitar,
    FingerBass, PickBass, SlapBass, FretlessBass, AcousticBass, SynthBass,
    Banjo, Mandolin, Ukulele, Sitar, Harp,
    Piano, EPiano, Organ, Mallets, Strings, Brass, Reed, Pipe, Choir,
    SynthLead, SynthPad, Drums, Other
};

const char* familyName (Family f);
bool isStringModelFamily (Family f);
bool isGuitarFamily (Family f);
bool isBassFamily (Family f);

struct Track
{
    std::string name;
    std::string soundHint;   // GP7 sound path / GP6 instrument ref, lowercase-friendly
    std::vector<int> tuning; // index 0 = lowest string, MIDI note numbers
    int   capo = 0;
    bool  percussion = false;
    int   program = 25;
    int   channel = 0;
    float volume = 0.8f;     // 0..1
    float pan    = 0.5f;     // 0 = left, 1 = right
    Family family = Family::SteelGuitar;
    std::vector<Bar> bars;   // one per master bar
};

struct Score
{
    std::string title, subtitle, artist, album, words, music, copyright, tab;
    std::string formatName;
    double tempo = 120.0;
    std::vector<MasterBar> masterBars;
    std::vector<Track> tracks;
    std::vector<TempoPoint> tempos;

    /** Fills Track::family from program/name/hints and sanitises the data. */
    void finalise();
};

Family classifyTrack (const Track& t);
} // namespace tc
