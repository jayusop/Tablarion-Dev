// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Reader for the binary Guitar Pro 3, 4 and 5 formats.
#include "GPReader.h"

#include <cmath>
#include <stdexcept>

namespace tc
{
namespace
{
struct ParseError : std::runtime_error { using std::runtime_error::runtime_error; };

class Stream
{
public:
    Stream (const uint8_t* d, size_t n) : data (d), size (n) {}

    void need (size_t n) const { if (pos + n > size) throw ParseError ("unexpected end of file"); }
    uint8_t  u8()  { need (1); return data[pos++]; }
    int8_t   i8()  { return (int8_t) u8(); }
    bool     boolean() { return u8() != 0; }
    int16_t  i16() { need (2); int16_t v = (int16_t) (data[pos] | (data[pos + 1] << 8)); pos += 2; return v; }
    int32_t  i32()
    {
        need (4);
        uint32_t v = (uint32_t) data[pos] | ((uint32_t) data[pos + 1] << 8) | ((uint32_t) data[pos + 2] << 16) | ((uint32_t) data[pos + 3] << 24);
        pos += 4;
        return (int32_t) v;
    }
    double f64() { need (8); double d; std::memcpy (&d, data + pos, 8); pos += 8; return d; }
    void skip (size_t n) { need (n); pos += n; }

    std::string chars (size_t n)
    {
        need (n);
        std::string s ((const char*) data + pos, n);
        pos += n;
        auto z = s.find ('\0');
        if (z != std::string::npos) s.resize (z);
        return latin1ToUtf8 (s);
    }

    /** byte length followed by a fixed-size field */
    std::string byteSizeString (size_t fieldSize)
    {
        int len = u8();
        auto s = chars (fieldSize);
        if ((size_t) len < s.size()) s.resize ((size_t) len);
        return s;
    }

    /** int length (= byte length + 1) followed by byte length and characters */
    std::string intByteSizeString()
    {
        int32_t n = i32();
        if (n < 0 || n > 100000) throw ParseError ("bad string");
        if (n == 0) return {};
        int len = u8();
        auto s = chars ((size_t) n - 1);
        if ((size_t) len < s.size()) s.resize ((size_t) len);
        return s;
    }

    std::string intSizeString()
    {
        int32_t n = i32();
        if (n < 0 || n > 1000000) throw ParseError ("bad string");
        return chars ((size_t) n);
    }

    bool peekIntByteSizeString() const
    {
        if (pos + 5 > size) return false;
        int32_t n = (int32_t) ((uint32_t) data[pos] | ((uint32_t) data[pos + 1] << 8) | ((uint32_t) data[pos + 2] << 16) | ((uint32_t) data[pos + 3] << 24));
        return n >= 1 && n < 256 && data[pos + 4] == n - 1;
    }

    static std::string latin1ToUtf8 (const std::string& in)
    {
        std::string out;
        out.reserve (in.size());
        for (unsigned char c : in)
        {
            if (c < 0x80) out += (char) c;
            else { out += (char) (0xC0 | (c >> 6)); out += (char) (0x80 | (c & 0x3F)); }
        }
        return out;
    }

    const uint8_t* data;
    size_t size;
    size_t pos = 0;
};

struct Channel { int program = 25; float volume = 0.8f; float pan = 0.5f; };

class GP345Parser
{
public:
    GP345Parser (const uint8_t* d, size_t n) : in (d, n) {}

    std::unique_ptr<Score> parse()
    {
        score = std::make_unique<Score>();
        readVersion();
        readInfo();

        TripletFeel globalTriplet = TripletFeel::None;
        if (version < 500)
            globalTriplet = in.boolean() ? TripletFeel::Eighth : TripletFeel::None;

        if (version >= 400) readLyrics();

        if (version >= 510) in.skip (19); // RSE master effect
        if (version >= 500) readPageSetup();

        if (version >= 500) in.intByteSizeString(); // tempo name
        score->tempo = in.i32();
        if (version >= 510) in.boolean(); // hide tempo

        if (version >= 500) { in.i8(); in.i32(); }      // key + octave
        else if (version >= 400) { in.i32(); in.i8(); } // key + octave
        else in.i32();                                   // key

        readMidiChannels();

        if (version >= 500)
        {
            in.skip (19 * 2); // directions
            in.i32();         // master reverb
        }

        const int measureCount = in.i32();
        const int trackCount   = in.i32();
        if (measureCount <= 0 || measureCount > 10000 || trackCount <= 0 || trackCount > 128)
            throw ParseError ("invalid measure/track count");

        for (int i = 0; i < measureCount; ++i) readMeasureHeader (i, globalTriplet);
        for (int i = 0; i < trackCount; ++i) readTrack (i);
        if (version >= 500) in.skip (version == 500 ? 2 : 1);

        for (auto& t : score->tracks) t.bars.resize ((size_t) measureCount);

        // Many files in the wild are truncated by a few bytes: keep everything read so far.
        try
        {
            for (int m = 0; m < measureCount; ++m)
                for (int t = 0; t < trackCount; ++t)
                    readMeasure (m, t);
        }
        catch (const ParseError&)
        {
            if (in.pos < in.size / 2) throw;
        }

        score->formatName = "Guitar Pro " + std::to_string (version / 100) + "." + std::to_string ((version % 100) / 10) + std::to_string (version % 10);
        return std::move (score);
    }

private:
    Stream in;
    std::unique_ptr<Score> score;
    int version = 500;
    Channel channels[64];
    std::vector<int> stringCounts;
    std::vector<std::vector<int>> lastFret; // for tied notes [track][string]

    void readVersion()
    {
        auto v = in.byteSizeString (30);
        if (v.find ("FICHIER GUITAR PRO") == std::string::npos && v.find ("FICHIER GUITARE PRO") == std::string::npos)
            throw ParseError ("not a Guitar Pro 3-5 file");
        auto p = v.find ('v');
        if (p == std::string::npos) throw ParseError ("unknown version");
        auto num = v.substr (p + 1);
        int major = 0, minor = 0;
        if (std::sscanf (num.c_str(), "%d.%d", &major, &minor) < 1) throw ParseError ("unknown version");
        version = major * 100 + minor;
        if (major < 3 || major > 5) throw ParseError ("unsupported version " + v);
    }

    void readInfo()
    {
        score->title    = in.intByteSizeString();
        score->subtitle = in.intByteSizeString();
        score->artist   = in.intByteSizeString();
        score->album    = in.intByteSizeString();
        score->words    = in.intByteSizeString();
        if (version >= 500) score->music = in.intByteSizeString();
        score->copyright = in.intByteSizeString();
        score->tab       = in.intByteSizeString();
        in.intByteSizeString(); // instructions
        int notices = in.i32();
        if (notices < 0 || notices > 1000) throw ParseError ("bad notice count");
        for (int i = 0; i < notices; ++i) in.intByteSizeString();
    }

    void readLyrics()
    {
        in.i32();
        for (int i = 0; i < 5; ++i) { in.i32(); in.intSizeString(); }
    }

    void readPageSetup()
    {
        in.skip (30);
        for (int i = 0; i < 10; ++i) in.intByteSizeString();
        // Some writers store an 11th header/footer string: detect it (the tempo name follows and is itself
        // a string, while the tempo integer after it is not).
        if (in.peekIntByteSizeString())
        {
            auto save = in.pos;
            in.intByteSizeString();
            const bool another = in.peekIntByteSizeString();
            in.pos = save;
            if (another) in.intByteSizeString();
        }
    }

    void readMidiChannels()
    {
        for (int i = 0; i < 64; ++i)
        {
            auto& c = channels[i];
            c.program = in.i32();
            int vol = in.i8(), bal = in.i8();
            in.skip (4 + 2);
            c.volume = std::clamp (vol / 16.0f, 0.0f, 1.0f);
            c.pan    = std::clamp (bal / 16.0f, 0.0f, 1.0f);
        }
    }

    void readMeasureHeader (int index, TripletFeel globalTriplet)
    {
        MasterBar mb;
        const MasterBar* prev = score->masterBars.empty() ? nullptr : &score->masterBars.back();
        if (prev) { mb.num = prev->num; mb.den = prev->den; }

        if (version >= 500 && index > 0) in.skip (1);

        const uint8_t flags = in.u8();
        if (flags & 0x01) mb.num = in.i8();
        if (flags & 0x02) mb.den = in.i8();
        mb.repeatStart = (flags & 0x04) != 0;
        if (flags & 0x08) mb.repeatCount = in.i8() + (version >= 500 ? 0 : 1);

        if ((flags & 0x10) && version < 500)
        {
            // GP3/4 store the number of the ending; build a mask excluding the endings already used.
            uint32_t existing = 0;
            for (int i = (int) score->masterBars.size() - 1; i >= 0; --i)
            {
                auto& b = score->masterBars[(size_t) i];
                if (b.repeatCount > 0 && i != (int) score->masterBars.size() - 1) break;
                existing |= b.alternateEndings;
                if (b.repeatStart) break;
            }
            int num = in.u8();
            uint32_t mask = 0;
            for (int i = 0; i < 8; ++i)
                if (num > i && (existing & (1u << i)) == 0) mask |= (1u << i);
            mb.alternateEndings = mask;
        }

        if (flags & 0x20)
        {
            mb.marker = in.intByteSizeString();
            in.skip (4); // colour
        }
        if (flags & 0x40) in.skip (2); // key signature
        if (version >= 500)
        {
            if (flags & 0x03) in.skip (4); // beam groups
            if (flags & 0x10) mb.alternateEndings = in.u8();
            else in.skip (1);
            int tf = in.u8();
            mb.triplet = tf == 1 ? TripletFeel::Eighth : (tf == 2 ? TripletFeel::Sixteenth : TripletFeel::None);
        }
        else
            mb.triplet = globalTriplet;

        score->masterBars.push_back (mb);
    }

    void readTrack (int index)
    {
        if (version >= 500 && (index == 0 || version == 500)) in.skip (1);
        Track t;
        const uint8_t flags = in.u8();
        t.percussion = (flags & 0x01) != 0;
        t.name = in.byteSizeString (40);
        int stringCount = in.i32();
        if (stringCount < 1 || stringCount > 7) stringCount = 6;
        std::vector<int> tuning;
        for (int i = 0; i < 7; ++i)
        {
            int v = in.i32();
            if (i < stringCount) tuning.push_back (v);
        }
        // file: string 1 (highest) first -> store lowest first
        t.tuning.assign (tuning.rbegin(), tuning.rend());
        int port = in.i32(), channel = in.i32();
        in.i32(); // effect channel
        in.i32(); // fret count
        t.capo = std::clamp (in.i32(), 0, 24);
        in.skip (4); // colour

        if (version >= 500)
        {
            in.skip (2 + 1 + 1); // settings flags, auto accentuation, bank
            in.skip (1 + 12 + 12); // humanise, 3 ints, 12 unknown bytes
            in.skip (4 + 4 + 4);   // RSE instrument, unknown, soundbank
            if (version == 500) in.skip (2 + 1);
            else
            {
                in.skip (4);     // effect number
                in.skip (4);     // equaliser (3 bands + gain)
                in.intByteSizeString(); // effect
                in.intByteSizeString(); // effect category
            }
        }

        const int ci = std::clamp ((port - 1) * 16 + (channel - 1), 0, 63);
        t.channel = std::clamp (channel - 1, 0, 15);
        if (t.channel == 9) t.percussion = true;
        t.program = channels[ci].program;
        t.volume  = channels[ci].volume;
        t.pan     = channels[ci].pan;

        stringCounts.push_back (stringCount);
        lastFret.push_back (std::vector<int> ((size_t) stringCount, 0));
        score->tracks.push_back (std::move (t));
    }

    void readMeasure (int m, int t)
    {
        auto& bar = score->tracks[(size_t) t].bars[(size_t) m];
        const int voices = version >= 500 ? 2 : 1;
        bar.voices.resize ((size_t) voices);
        for (int v = 0; v < voices; ++v)
        {
            int beats = in.i32();
            if (beats < 0 || beats > 512) throw ParseError ("invalid beat count");
            for (int b = 0; b < beats; ++b)
                readBeat (m, t, bar.voices[(size_t) v]);
        }
        if (version >= 500 && in.pos < in.size) in.skip (1); // line break
    }

    static int tupletTimes (int n)
    {
        switch (n)
        {
            case 3: return 2;
            case 5: case 6: case 7: return 4;
            case 9: case 10: case 11: case 12: case 13: return 8;
            default: return n;
        }
    }

    void readChord()
    {
        const bool newFormat = (in.u8() & 0x01) != 0;
        if (! newFormat)
        {
            in.intByteSizeString();
            int firstFret = in.i32();
            if (firstFret != 0) in.skip (6 * 4);
        }
        else if (version >= 400)
        {
            in.skip (16);
            in.byteSizeString (21);
            in.skip (4);
            in.i32();
            in.skip (7 * 4);
            in.skip (32);
        }
        else
        {
            in.skip (25);
            in.byteSizeString (34);
            in.i32();
            in.skip (6 * 4);
            in.skip (36);
        }
    }

    std::vector<BendPoint> readBend()
    {
        in.i8();  // type
        in.i32(); // value
        int n = in.i32();
        if (n < 0 || n > 100) throw ParseError ("bad bend");
        std::vector<BendPoint> pts;
        for (int i = 0; i < n; ++i)
        {
            int pos = in.i32(), val = in.i32();
            in.boolean();
            pts.push_back ({ std::clamp (pos / 60.0f, 0.0f, 1.0f), val / 50.0f });
        }
        return pts;
    }

    void readBeatEffects (Beat& beat)
    {
        if (version >= 400)
        {
            const uint8_t f1 = in.u8(), f2 = in.u8();
            beat.fadeIn = (f1 & 0x10) != 0;
            if (f1 & 0x20)
            {
                int s = in.i8();
                beat.tap = s == 1; beat.slap = s == 2; beat.pop = s == 3;
            }
            if (f2 & 0x04) beat.whammy = readBend();
            if (f1 & 0x40) readStroke (beat);
            beat.rasgueado = (f2 & 0x01) != 0;
            if (f2 & 0x02)
            {
                int ps = in.i8();
                beat.pickStroke = ps == 1 ? Stroke::Up : (ps == 2 ? Stroke::Down : Stroke::None);
            }
        }
        else
        {
            const uint8_t f1 = in.u8();
            beat.fadeIn = (f1 & 0x10) != 0;
            if (f1 & 0x20)
            {
                int type = in.u8();
                if (type == 0)
                {
                    int dip = in.i32();
                    beat.whammy = { { 0.0f, 0.0f }, { 0.5f, -dip / 50.0f }, { 1.0f, 0.0f } };
                }
                else
                {
                    beat.tap = type == 1; beat.slap = type == 2; beat.pop = type == 3;
                    in.i32();
                }
            }
            if (f1 & 0x40) readStroke (beat);
        }
    }

    void readStroke (Beat& beat)
    {
        int a = in.i8(), b = in.i8();
        int down = version >= 500 ? b : a;
        int up   = version >= 500 ? a : b;
        auto toTicks = [] (int v)
        {
            switch (v)
            {
                case 1: return PPQ / 32; case 2: return PPQ / 16; case 3: return PPQ / 8;
                case 4: return PPQ / 4;  case 5: return PPQ / 2;  case 6: return PPQ;
                default: return PPQ / 8;
            }
        };
        if (up > 0) { beat.brush = Stroke::Up; beat.brushTicks = toTicks (up); }
        else if (down > 0) { beat.brush = Stroke::Down; beat.brushTicks = toTicks (down); }
    }

    void readMixTable (Beat& beat)
    {
        in.i8(); // instrument
        if (version >= 500) in.skip (16);
        int vals[6];
        for (auto& v : vals) v = in.i8();
        if (version >= 500) in.intByteSizeString();
        int tempo = in.i32();
        for (int v : vals) if (v >= 0) in.skip (1);
        if (tempo >= 0)
        {
            in.skip (1);
            if (version >= 510) in.skip (1);
            if (tempo > 0) beat.tempo = tempo;
        }
        if (version >= 400) in.skip (1);
        if (version >= 500) in.skip (1);
        if (version >= 510) { in.intByteSizeString(); in.intByteSizeString(); }
    }

    static float unpackVelocity (int dyn) { return std::clamp ((15 + 16 * dyn - 16) / 127.0f, 0.05f, 1.0f); }

    void readNoteEffects (Note& note, int trackIndex)
    {
        (void) trackIndex;
        if (version >= 400)
        {
            const uint8_t f1 = in.u8(), f2 = in.u8();
            if (f1 & 0x01) note.bend = readBend();
            note.hopoOrigin = (f1 & 0x02) != 0;
            note.letRing    = (f1 & 0x08) != 0;
            if (f1 & 0x10) in.skip (version >= 500 ? 5 : 4); // grace note (approximated below)
            note.staccato = (f2 & 0x01) != 0;
            note.palmMute = (f2 & 0x02) != 0;
            if (f2 & 0x04)
            {
                int v = in.i8();
                pendingTremolo = v == 1 ? PPQ / 2 : (v == 2 ? PPQ / 4 : PPQ / 8);
            }
            if (f2 & 0x08)
            {
                if (version >= 500) note.slide = in.u8();
                else
                {
                    int s = in.i8();
                    switch (s)
                    {
                        case 1: note.slide = SlideShift; break;
                        case 2: note.slide = SlideLegato; break;
                        case 3: note.slide = SlideOutDown; break;
                        case 4: note.slide = SlideOutUp; break;
                        case -1: note.slide = SlideInBelow; break;
                        case -2: note.slide = SlideInAbove; break;
                        default: break;
                    }
                }
            }
            if (f2 & 0x10)
            {
                int h = in.i8();
                if (version >= 500)
                {
                    switch (h)
                    {
                        case 1: note.harmonic = Harmonic::Natural; break;
                        case 2: note.harmonic = Harmonic::Artificial; in.skip (3); note.harmonicFret = 12; break;
                        case 3: note.harmonic = Harmonic::Tapped; note.harmonicFret = (float) in.u8(); break;
                        case 4: note.harmonic = Harmonic::Pinch; break;
                        case 5: note.harmonic = Harmonic::Semi; break;
                        default: break;
                    }
                }
                else
                {
                    if (h == 1) note.harmonic = Harmonic::Natural;
                    else if (h == 3) { note.harmonic = Harmonic::Tapped; note.harmonicFret = 12; }
                    else if (h == 4) note.harmonic = Harmonic::Pinch;
                    else if (h == 5) note.harmonic = Harmonic::Semi;
                    else if (h >= 15) { note.harmonic = Harmonic::Artificial; note.harmonicFret = 12; }
                }
            }
            if (f2 & 0x20)
            {
                int fret = in.i8(), period = in.i8();
                note.trillMidi  = fret; // converted to MIDI later
                note.trillTicks = period == 1 ? PPQ / 4 : (period == 2 ? PPQ / 8 : PPQ / 16);
            }
            if (f2 & 0x40) note.vibrato = 1;
        }
        else
        {
            const uint8_t f = in.u8();
            note.hopoOrigin = (f & 0x02) != 0;
            note.letRing    = (f & 0x08) != 0;
            if (f & 0x01) note.bend = readBend();
            if (f & 0x10) in.skip (4);
            if (f & 0x04) note.slide = SlideShift;
        }
    }

    int pendingTremolo = 0;

    void readNote (Beat& beat, int trackIndex, int stringIndexFromTop)
    {
        auto& track = score->tracks[(size_t) trackIndex];
        const int stringCount = stringCounts[(size_t) trackIndex];
        Note note;
        note.string = stringCount - 1 - stringIndexFromTop; // 0 = lowest
        const uint8_t flags = in.u8();
        note.heavyAccent = (flags & 0x02) != 0;
        note.ghost       = (flags & 0x04) != 0;
        note.accent      = version >= 400 && (flags & 0x40) != 0;

        int type = 1;
        if (flags & 0x20) type = in.u8();
        if (version < 500 && (flags & 0x01)) in.skip (2);
        if (flags & 0x10) note.velocity = unpackVelocity (in.i8());
        int fret = 0;
        if (flags & 0x20) fret = in.i8();
        if (flags & 0x80) in.skip (2);
        if (version >= 500 && (flags & 0x01)) in.f64();
        if (version >= 500) in.skip (1);
        if (flags & 0x08) readNoteEffects (note, trackIndex);

        auto& lf = lastFret[(size_t) trackIndex];
        const int si = std::clamp (note.string, 0, stringCount - 1);
        if (type == 2) { note.tieDest = true; fret = lf[(size_t) si]; }
        if (type == 3) note.dead = true;
        fret = std::clamp (fret, 0, 99);
        lf[(size_t) si] = fret;
        note.fret = fret;

        if (track.percussion)
        {
            note.midi = fret;
            note.string = -1;
        }
        else
        {
            const int open = si < (int) track.tuning.size() ? track.tuning[(size_t) si] : 40;
            note.midi = open + track.capo + fret;
            if (note.trillMidi >= 0) note.trillMidi = open + track.capo + note.trillMidi;
        }
        beat.notes.push_back (std::move (note));
    }

    void readBeat (int m, int t, Voice& voice)
    {
        (void) m;
        Beat beat;
        const uint8_t flags = in.u8();
        bool rest = false;
        if (flags & 0x40) { int status = in.u8(); rest = true; (void) status; }
        int d = in.i8();
        d = std::clamp (d, -2, 6);
        double ticks = PPQ * std::pow (2.0, -d);
        if (flags & 0x01) ticks *= 1.5;
        if (flags & 0x20)
        {
            int n = in.i32();
            if (n > 1 && n < 32) ticks = ticks * tupletTimes (n) / n;
        }
        beat.duration = std::max (1, (int) std::lround (ticks));
        if (flags & 0x02) readChord();
        if (flags & 0x04) in.intByteSizeString();
        if (flags & 0x08) readBeatEffects (beat);
        if (flags & 0x10) readMixTable (beat);

        pendingTremolo = 0;
        const uint8_t stringFlags = in.u8();
        for (int i = 6; i >= 0; --i)
        {
            if (stringFlags & (1 << i))
            {
                const int stringNumber = 7 - i; // 1 = highest
                readNote (beat, t, stringNumber - 1);
            }
        }
        beat.tremoloTicks = pendingTremolo;

        if (version >= 500)
        {
            int16_t f2 = in.i16();
            if (f2 & 0x0800) in.skip (1);
        }

        beat.rest = rest || beat.notes.empty();
        voice.beats.push_back (std::move (beat));
    }
};
} // namespace

std::unique_ptr<Score> readGP345 (const uint8_t* data, size_t size, juce::String& error)
{
    try
    {
        GP345Parser p (data, size);
        auto s = p.parse();
        // Tempo changes from mix tables -> tempo map
        for (size_t ti = 0; ti < s->tracks.size(); ++ti)
        {
            auto& tr = s->tracks[ti];
            for (size_t m = 0; m < tr.bars.size(); ++m)
            {
                const int len = s->masterBars[m].lengthTicks();
                for (auto& v : tr.bars[m].voices)
                {
                    int pos = 0;
                    for (auto& b : v.beats)
                    {
                        if (b.tempo > 0)
                            s->tempos.push_back ({ (int) m, len > 0 ? (float) pos / (float) len : 0.0f, b.tempo });
                        pos += b.duration;
                    }
                }
            }
        }
        s->finalise();
        return s;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return nullptr;
    }
}
} // namespace tc
