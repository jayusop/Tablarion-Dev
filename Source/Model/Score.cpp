// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
#include "Score.h"

#include <algorithm>
#include <cctype>

namespace tc
{
const char* familyName (Family f)
{
    switch (f)
    {
        case Family::SteelGuitar:      return "Steel Guitar";
        case Family::NylonGuitar:      return "Nylon Guitar";
        case Family::CleanElectric:    return "Clean Electric";
        case Family::JazzElectric:     return "Jazz Guitar";
        case Family::MutedElectric:    return "Muted Electric";
        case Family::OverdriveGuitar:  return "Overdrive Guitar";
        case Family::DistortionGuitar: return "Distortion Guitar";
        case Family::HarmonicsGuitar:  return "Harmonics Guitar";
        case Family::FingerBass:       return "Finger Bass";
        case Family::PickBass:         return "Pick Bass";
        case Family::SlapBass:         return "Slap Bass";
        case Family::FretlessBass:     return "Fretless Bass";
        case Family::AcousticBass:     return "Acoustic Bass";
        case Family::SynthBass:        return "Synth Bass";
        case Family::Banjo:            return "Banjo";
        case Family::Mandolin:         return "Mandolin";
        case Family::Ukulele:          return "Ukulele";
        case Family::Sitar:            return "Sitar";
        case Family::Harp:             return "Harp";
        case Family::Piano:            return "Grand Piano";
        case Family::EPiano:           return "Electric Piano";
        case Family::Organ:            return "Organ";
        case Family::Mallets:          return "Mallets";
        case Family::Strings:          return "Strings";
        case Family::Brass:            return "Brass";
        case Family::Reed:             return "Reed";
        case Family::Pipe:             return "Flute";
        case Family::Choir:            return "Choir";
        case Family::SynthLead:        return "Synth Lead";
        case Family::SynthPad:         return "Synth Pad";
        case Family::Drums:            return "Drum Kit";
        case Family::Other:            return "Synth";
    }
    return "Synth";
}

bool isGuitarFamily (Family f)
{
    switch (f)
    {
        case Family::SteelGuitar: case Family::NylonGuitar: case Family::CleanElectric:
        case Family::JazzElectric: case Family::MutedElectric: case Family::OverdriveGuitar:
        case Family::DistortionGuitar: case Family::HarmonicsGuitar:
            return true;
        default: return false;
    }
}

bool isBassFamily (Family f)
{
    switch (f)
    {
        case Family::FingerBass: case Family::PickBass: case Family::SlapBass:
        case Family::FretlessBass: case Family::AcousticBass:
            return true;
        default: return false;
    }
}

bool isStringModelFamily (Family f)
{
    if (isGuitarFamily (f) || isBassFamily (f)) return true;
    switch (f)
    {
        case Family::Banjo: case Family::Mandolin: case Family::Ukulele:
        case Family::Sitar: case Family::Harp:
            return true;
        default: return false;
    }
}

static std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

static bool has (const std::string& s, const char* key) { return s.find (key) != std::string::npos; }

static Family familyFromProgram (int p)
{
    if (p < 0) return Family::SteelGuitar;
    if (p <= 3) return Family::Piano;
    if (p <= 5) return Family::EPiano;
    if (p <= 7) return Family::Piano;
    if (p <= 15) return Family::Mallets;
    if (p <= 23) return Family::Organ;
    switch (p)
    {
        case 24: return Family::NylonGuitar;
        case 25: return Family::SteelGuitar;
        case 26: return Family::JazzElectric;
        case 27: return Family::CleanElectric;
        case 28: return Family::MutedElectric;
        case 29: return Family::OverdriveGuitar;
        case 30: return Family::DistortionGuitar;
        case 31: return Family::HarmonicsGuitar;
        case 32: return Family::AcousticBass;
        case 33: return Family::FingerBass;
        case 34: return Family::PickBass;
        case 35: return Family::FretlessBass;
        case 36: case 37: return Family::SlapBass;
        case 38: case 39: return Family::SynthBass;
        case 46: return Family::Harp;
        case 104: return Family::Sitar;
        case 105: return Family::Banjo;
        case 106: case 107: return Family::Banjo;
        case 108: return Family::Mallets;
        default: break;
    }
    if (p <= 51) return Family::Strings;
    if (p <= 54) return Family::Choir;
    if (p == 55) return Family::Strings;
    if (p <= 63) return Family::Brass;
    if (p <= 71) return Family::Reed;
    if (p <= 79) return Family::Pipe;
    if (p <= 87) return Family::SynthLead;
    if (p <= 95) return Family::SynthPad;
    if (p >= 112 && p <= 119) return Family::Mallets;
    return Family::Other;
}

Family classifyTrack (const Track& t)
{
    if (t.percussion) return Family::Drums;

    const auto hint = lower (t.soundHint);
    const auto name = lower (t.name);
    const auto any  = hint + " " + name;

    // GP6/GP7 sound hints first: they are the most precise ("Stringed/Electric Guitars/Clean Guitar", "e-gtr6"...)
    if (! hint.empty())
    {
        if (has (hint, "drum") || has (hint, "percussion")) return Family::Drums;
        if (has (hint, "bass"))
        {
            if (has (hint, "slap")) return Family::SlapBass;
            if (has (hint, "fretless")) return Family::FretlessBass;
            if (has (hint, "pick")) return Family::PickBass;
            if (has (hint, "synth")) return Family::SynthBass;
            if (has (hint, "acoustic") || has (hint, "upright") || has (hint, "contrabass")) return Family::AcousticBass;
            return Family::FingerBass;
        }
        if (has (hint, "nylon") || has (hint, "classical") || has (hint, "n-gtr")) return Family::NylonGuitar;
        if (has (hint, "steel") || has (hint, "acoustic guitar") || has (hint, "s-gtr") || has (hint, "12 string")) return Family::SteelGuitar;
        if (has (hint, "distortion") || has (hint, "metal") || has (hint, "high gain") || has (hint, "hi-gain")) return Family::DistortionGuitar;
        if (has (hint, "overdrive") || has (hint, "crunch") || has (hint, "lead") || has (hint, "blues")) return Family::OverdriveGuitar;
        if (has (hint, "jazz")) return Family::JazzElectric;
        if (has (hint, "electric guitar") || has (hint, "e-gtr") || has (hint, "clean")) return Family::CleanElectric;
        if (has (hint, "banjo")) return Family::Banjo;
        if (has (hint, "mandolin")) return Family::Mandolin;
        if (has (hint, "ukulele") || has (hint, "ukelele")) return Family::Ukulele;
        if (has (hint, "sitar")) return Family::Sitar;
        if (has (hint, "harp")) return Family::Harp;
        if (has (hint, "piano") || has (hint, "keyboard")) return has (hint, "electric") ? Family::EPiano : Family::Piano;
        if (has (hint, "organ")) return Family::Organ;
    }

    Family f = familyFromProgram (t.program);

    // Name heuristics help with mis-programmed GP3/4 files.
    if (has (any, "bass") && ! isBassFamily (f) && f != Family::SynthBass && t.tuning.size() <= 6
        && ! t.tuning.empty() && t.tuning.front() < 36)
        f = Family::FingerBass;
    if (has (name, "drum") || has (name, "batter")) f = Family::Drums;

    // Stringed instrument but pitched like a bass?
    if (isGuitarFamily (f) && ! t.tuning.empty() && t.tuning.size() <= 6 && t.tuning.back() <= 48)
        f = Family::FingerBass;

    return f;
}

void Score::finalise()
{
    if (tempo <= 0.0 || tempo > 1000.0) tempo = 120.0;
    if (masterBars.empty()) masterBars.push_back ({});

    for (auto& mb : masterBars)
    {
        if (mb.num <= 0 || mb.num > 64) mb.num = 4;
        if (mb.den != 1 && mb.den != 2 && mb.den != 4 && mb.den != 8 && mb.den != 16 && mb.den != 32) mb.den = 4;
    }

    for (auto& t : tracks)
    {
        t.bars.resize (masterBars.size());
        t.family = classifyTrack (t);
        if (t.family == Family::Drums) t.percussion = true;
        t.volume = std::clamp (t.volume, 0.0f, 1.0f);
        t.pan    = std::clamp (t.pan, 0.0f, 1.0f);
    }

    std::sort (tempos.begin(), tempos.end(), [] (const TempoPoint& a, const TempoPoint& b)
               { return a.bar != b.bar ? a.bar < b.bar : a.pos < b.pos; });
}
} // namespace tc
