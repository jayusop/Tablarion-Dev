// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Reader for the GPIF XML format used by Guitar Pro 6 (.gpx), 7 and 8 (.gp)
#include "GPReader.h"

#include <cmath>
#include <unordered_map>

namespace tc
{
namespace
{
using juce::String;
using juce::XmlElement;

const XmlElement* child (const XmlElement* e, const char* name) { return e ? e->getChildByName (name) : nullptr; }

String text (const XmlElement* e, const char* name)
{
    if (auto* c = child (e, name)) return c->getAllSubText().trim();
    return {};
}

std::vector<int> ints (const String& s)
{
    std::vector<int> r;
    juce::StringArray tok;
    tok.addTokens (s, " \t\r\n", "");
    tok.removeEmptyStrings();
    for (auto& t : tok) r.push_back (t.getIntValue());
    return r;
}

std::vector<float> floats (const String& s)
{
    std::vector<float> r;
    juce::StringArray tok;
    tok.addTokens (s, " \t\r\n", "");
    tok.removeEmptyStrings();
    for (auto& t : tok) r.push_back (t.getFloatValue());
    return r;
}

const XmlElement* property (const XmlElement* node, const char* name)
{
    if (auto* props = child (node, "Properties"))
        for (auto* p : props->getChildIterator())
            if (p->getStringAttribute ("name") == name) return p;
    return nullptr;
}

bool propertyEnabled (const XmlElement* node, const char* name)
{
    auto* p = property (node, name);
    return p != nullptr && (child (p, "Enable") != nullptr || p->getNumChildElements() == 0 || text (p, "Enable").isEmpty());
}

String propertyText (const XmlElement* node, const char* name, const char* field)
{
    if (auto* p = property (node, name)) return text (p, field);
    return {};
}

bool hasPropertyField (const XmlElement* node, const char* name, const char* field)
{
    auto* p = property (node, name);
    return p != nullptr && child (p, field) != nullptr;
}

int stepToSemitone (const String& step)
{
    if (step.isEmpty()) return 0;
    switch (juce::CharacterFunctions::toUpperCase (step[0]))
    {
        case 'C': return 0; case 'D': return 2; case 'E': return 4; case 'F': return 5;
        case 'G': return 7; case 'A': return 9; case 'B': return 11;
        default: return 0;
    }
}

int accidentalToSemitone (const String& a)
{
    if (a == "#" || a == "Sharp") return 1;
    if (a == "b" || a == "Flat") return -1;
    if (a == "##" || a == "x" || a == "DoubleSharp") return 2;
    if (a == "bb" || a == "DoubleFlat") return -2;
    return 0;
}

// Guitar Pro 6 drum "element/variation" -> General MIDI
int gp6DrumToMidi (int element, int variation)
{
    static const int table[][3] = {
        { 35, 35, 35 }, { 38, 40, 37 }, { 56, 56, 56 }, { 56, 56, 56 }, { 56, 56, 56 },
        { 41, 41, 41 }, { 45, 45, 45 }, { 47, 47, 47 }, { 48, 48, 48 }, { 50, 50, 50 },
        { 42, 44, 46 }, { 44, 44, 44 }, { 57, 57, 57 }, { 49, 49, 49 }, { 55, 55, 55 },
        { 51, 59, 53 }, { 52, 52, 52 }
    };
    if (element < 0 || element >= (int) (sizeof (table) / sizeof (table[0]))) return 38;
    return table[element][std::clamp (variation, 0, 2)];
}

int normaliseDrumMidi (int m)
{
    // Guitar Pro 7 articulation numbers outside the GM range
    switch (m)
    {
        case 91: return 40;  // snare rim shot
        case 92: return 46;  // hi-hat half open
        case 93: return 51;  // ride edge
        case 94: return 51;
        case 95: return 55;  // splash choke
        case 96: return 52;  // china choke
        case 97: return 49;  // crash high choke
        case 98: return 57;  // crash medium choke
        case 99: case 100: case 101: case 102: case 103: case 104: return 56;
        case 105: case 106: return 54;
        case 127: return 53; // ride bell
        default: break;
    }
    return std::clamp (m, 27, 87);
}

float dynamicToVelocity (const String& d)
{
    if (d == "PPP") return 0.16f;
    if (d == "PP")  return 0.26f;
    if (d == "P")   return 0.38f;
    if (d == "MP")  return 0.52f;
    if (d == "MF")  return 0.64f;
    if (d == "F")   return 0.76f;
    if (d == "FF")  return 0.88f;
    if (d == "FFF") return 1.0f;
    return -1.0f;
}

int rhythmTicks (const XmlElement* r)
{
    if (r == nullptr) return PPQ;
    const String v = text (r, "NoteValue");
    double t = PPQ;
    if (v == "Long") t = PPQ * 16;
    else if (v == "DoubleWhole") t = PPQ * 8;
    else if (v == "Whole") t = PPQ * 4;
    else if (v == "Half") t = PPQ * 2;
    else if (v == "Quarter") t = PPQ;
    else if (v == "Eighth") t = PPQ / 2.0;
    else if (v == "16th") t = PPQ / 4.0;
    else if (v == "32nd") t = PPQ / 8.0;
    else if (v == "64th") t = PPQ / 16.0;
    else if (v == "128th") t = PPQ / 32.0;
    else if (v == "256th") t = PPQ / 64.0;

    if (auto* dot = child (r, "AugmentationDot"))
    {
        const int n = dot->getIntAttribute ("count", 1);
        double add = t / 2.0, total = t;
        for (int i = 0; i < n; ++i) { total += add; add /= 2.0; }
        t = total;
    }
    if (auto* tup = child (r, "PrimaryTuplet"))
    {
        const int num = tup->getIntAttribute ("num", 1), den = tup->getIntAttribute ("den", 1);
        if (num > 0 && den > 0) t = t * den / num;
    }
    if (auto* tup = child (r, "SecondaryTuplet"))
    {
        const int num = tup->getIntAttribute ("num", 1), den = tup->getIntAttribute ("den", 1);
        if (num > 0 && den > 0) t = t * den / num;
    }
    return std::max (1, (int) std::lround (t));
}

using IdMap = std::unordered_map<int, const XmlElement*>;

IdMap mapById (const XmlElement* parent, const char* childName)
{
    IdMap m;
    if (parent)
        for (auto* c : parent->getChildWithTagNameIterator (childName))
            m[c->getIntAttribute ("id", -1)] = c;
    return m;
}

const XmlElement* find (const IdMap& m, int id)
{
    auto it = m.find (id);
    return it == m.end() ? nullptr : it->second;
}

struct TrackInfo
{
    int staffCount = 1;
    std::vector<int> drumArticulations; // GP7 articulation index -> MIDI
};

std::vector<BendPoint> readBendCurve (const XmlElement* node, const char* prefix)
{
    auto f = [&] (const char* suffix, float def) -> float
    {
        String name = String (prefix) + suffix;
        auto* p = property (node, name.toRawUTF8());
        if (p == nullptr) return def;
        return text (p, "Float").getFloatValue();
    };
    auto has = [&] (const char* suffix) { return property (node, (String (prefix) + suffix).toRawUTF8()) != nullptr; };

    std::vector<BendPoint> pts;
    pts.push_back ({ f ("OriginOffset", 0.0f) / 100.0f, f ("OriginValue", 0.0f) / 50.0f });
    if (has ("MiddleValue"))
    {
        const float mv = f ("MiddleValue", 0.0f) / 50.0f;
        pts.push_back ({ f ("MiddleOffset1", 50.0f) / 100.0f, mv });
        if (has ("MiddleOffset2")) pts.push_back ({ f ("MiddleOffset2", 50.0f) / 100.0f, mv });
    }
    pts.push_back ({ f ("DestinationOffset", 100.0f) / 100.0f, f ("DestinationValue", 0.0f) / 50.0f });
    std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.pos < b.pos; });
    for (auto& p : pts) p.pos = std::clamp (p.pos, 0.0f, 1.0f);
    return pts;
}

class GPIFParser
{
public:
    std::unique_ptr<Score> parse (const String& xmlText, String& error)
    {
        juce::XmlDocument doc (xmlText);
        auto root = doc.getDocumentElement();
        if (root == nullptr || ! root->hasTagName ("GPIF"))
        {
            error = "invalid GPIF document: " + doc.getLastParseError();
            return nullptr;
        }

        score = std::make_unique<Score>();
        readScoreInfo (child (root.get(), "Score"));

        rhythms = mapById (child (root.get(), "Rhythms"), "Rhythm");
        notes   = mapById (child (root.get(), "Notes"), "Note");
        beats   = mapById (child (root.get(), "Beats"), "Beat");
        voices  = mapById (child (root.get(), "Voices"), "Voice");
        bars    = mapById (child (root.get(), "Bars"), "Bar");

        readTracks (child (root.get(), "Tracks"));
        readMasterTrack (child (root.get(), "MasterTrack"));
        readMasterBars (child (root.get(), "MasterBars"));

        if (score->tracks.empty()) { error = "no tracks"; return nullptr; }
        score->finalise();
        return std::move (score);
    }

private:
    std::unique_ptr<Score> score;
    IdMap rhythms, notes, beats, voices, bars;
    std::vector<TrackInfo> infos;

    void readScoreInfo (const XmlElement* s)
    {
        if (! s) return;
        score->title     = text (s, "Title").toStdString();
        score->subtitle  = text (s, "SubTitle").toStdString();
        score->artist    = text (s, "Artist").toStdString();
        score->album     = text (s, "Album").toStdString();
        score->words     = text (s, "Words").toStdString();
        score->music     = text (s, "Music").toStdString();
        score->copyright = text (s, "Copyright").toStdString();
        score->tab       = text (s, "Tabber").toStdString();
    }

    void readTracks (const XmlElement* tracks)
    {
        if (! tracks) return;
        for (auto* tr : tracks->getChildWithTagNameIterator ("Track"))
        {
            Track t;
            TrackInfo info;
            t.name = text (tr, "Name").toStdString();
            if (t.name.empty()) t.name = text (tr, "ShortName").toStdString();

            // Tuning & capo: GP7 keeps them per staff, GP6 per track
            const XmlElement* tuningHolder = tr;
            if (auto* staves = child (tr, "Staves"))
            {
                info.staffCount = std::max (1, staves->getNumChildElements());
                if (auto* staff = staves->getChildByName ("Staff")) tuningHolder = staff;
            }
            auto pitches = ints (propertyText (tuningHolder, "Tuning", "Pitches"));
            if (pitches.empty()) pitches = ints (propertyText (tr, "Tuning", "Pitches"));
            t.tuning = pitches;
            String capo = propertyText (tuningHolder, "CapoFret", "Fret");
            if (capo.isEmpty()) capo = propertyText (tr, "CapoFret", "Fret");
            t.capo = std::clamp (capo.getIntValue(), 0, 24);

            // Sound / program
            String hint;
            if (auto* sounds = child (tr, "Sounds"))
                if (auto* snd = sounds->getChildByName ("Sound"))
                {
                    hint = text (snd, "Path") + " " + text (snd, "Name") + " " + text (snd, "Label");
                    if (auto* midi = child (snd, "MIDI"))
                        t.program = text (midi, "Program").getIntValue();
                }
            if (auto* gm = child (tr, "GeneralMidi"))
            {
                t.program = text (gm, "Program").getIntValue();
                t.channel = text (gm, "PrimaryChannel").getIntValue();
                if (gm->getStringAttribute ("table") == "Percussion") t.percussion = true;
            }
            if (auto* mc = child (tr, "MidiConnection"))
                t.channel = text (mc, "PrimaryChannel").getIntValue();
            if (auto* inst = child (tr, "Instrument"))
                hint += " " + inst->getStringAttribute ("ref");

            if (auto* iset = child (tr, "InstrumentSet"))
            {
                const String type = text (iset, "Type");
                hint += " " + text (iset, "Name") + " " + type;
                if (type.containsIgnoreCase ("drum") || type.containsIgnoreCase ("percussion")) t.percussion = true;
                if (auto* elements = child (iset, "Elements"))
                    for (auto* el : elements->getChildWithTagNameIterator ("Element"))
                        if (auto* arts = child (el, "Articulations"))
                            for (auto* art : arts->getChildWithTagNameIterator ("Articulation"))
                                info.drumArticulations.push_back (text (art, "OutputMidiNumber").getIntValue());
            }
            if (t.channel == 9) t.percussion = true;
            t.soundHint = hint.trim().toStdString();

            // Mixer: ChannelStrip parameters [11] = pan, [12] = volume
            const XmlElement* strip = child (tr, "ChannelStrip");
            if (! strip) if (auto* rse = child (tr, "RSE")) strip = child (rse, "ChannelStrip");
            if (strip)
            {
                auto p = floats (text (strip, "Parameters"));
                if (p.size() > 12) { t.pan = p[11]; t.volume = p[12]; }
            }

            infos.push_back (info);
            score->tracks.push_back (std::move (t));
        }
    }

    void readMasterTrack (const XmlElement* mt)
    {
        if (! mt) return;
        if (auto* autos = child (mt, "Automations"))
        {
            for (auto* a : autos->getChildWithTagNameIterator ("Automation"))
            {
                if (text (a, "Type") != "Tempo") continue;
                auto vals = floats (text (a, "Value"));
                if (vals.empty()) continue;
                double bpm = vals[0];
                if (vals.size() > 1)
                {
                    switch ((int) vals[1])
                    {
                        case 1: bpm *= 0.5; break;
                        case 3: bpm *= 1.5; break;
                        case 4: bpm *= 2.0; break;
                        case 5: bpm *= 3.0; break;
                        default: break;
                    }
                }
                TempoPoint tp;
                tp.bar = text (a, "Bar").getIntValue();
                tp.pos = std::clamp (text (a, "Position").getFloatValue(), 0.0f, 1.0f);
                tp.bpm = bpm;
                score->tempos.push_back (tp);
            }
        }
        for (auto& tp : score->tempos)
            if (tp.bar == 0 && tp.pos <= 0.0f) { score->tempo = tp.bpm; break; }
    }

    void readMasterBars (const XmlElement* mbs)
    {
        if (! mbs) return;
        int index = 0;
        for (auto* m : mbs->getChildWithTagNameIterator ("MasterBar"))
        {
            MasterBar mb;
            auto time = text (m, "Time");
            if (time.containsChar ('/'))
            {
                mb.num = time.upToFirstOccurrenceOf ("/", false, false).getIntValue();
                mb.den = time.fromFirstOccurrenceOf ("/", false, false).getIntValue();
            }
            if (auto* rep = child (m, "Repeat"))
            {
                mb.repeatStart = rep->getBoolAttribute ("start");
                if (rep->getBoolAttribute ("end")) mb.repeatCount = std::max (2, rep->getIntAttribute ("count", 2));
            }
            for (int e : ints (text (m, "AlternateEndings")))
                if (e >= 1 && e <= 32) mb.alternateEndings |= (1u << (e - 1));
            auto tf = text (m, "TripletFeel");
            if (tf == "Triplet8th") mb.triplet = TripletFeel::Eighth;
            else if (tf == "Triplet16th") mb.triplet = TripletFeel::Sixteenth;
            if (auto* sec = child (m, "Section")) mb.marker = text (sec, "Text").toStdString();
            score->masterBars.push_back (mb);

            auto barIds = ints (text (m, "Bars"));
            size_t cursor = 0;
            for (size_t ti = 0; ti < score->tracks.size(); ++ti)
            {
                auto& track = score->tracks[ti];
                track.bars.resize ((size_t) index + 1);
                Bar& bar = track.bars[(size_t) index];
                for (int s = 0; s < infos[ti].staffCount; ++s, ++cursor)
                {
                    if (cursor >= barIds.size()) break;
                    readBar (find (bars, barIds[cursor]), (int) ti, bar);
                }
            }
            ++index;
        }
    }

    void readBar (const XmlElement* barEl, int trackIndex, Bar& out)
    {
        if (! barEl) return;
        for (int vid : ints (text (barEl, "Voices")))
        {
            if (vid < 0) continue;
            auto* v = find (voices, vid);
            if (! v) continue;
            Voice voice;
            for (int bid : ints (text (v, "Beats")))
                if (auto* b = find (beats, bid))
                    voice.beats.push_back (readBeat (b, trackIndex));
            out.voices.push_back (std::move (voice));
        }
    }

    static Stroke strokeFrom (const String& s)
    {
        if (s == "Up") return Stroke::Up;
        if (s == "Down") return Stroke::Down;
        return Stroke::None;
    }

    Beat readBeat (const XmlElement* b, int trackIndex)
    {
        Beat beat;
        if (auto* r = child (b, "Rhythm")) beat.duration = rhythmTicks (find (rhythms, r->getIntAttribute ("ref", -1)));
        beat.dynamic = dynamicToVelocity (text (b, "Dynamic"));

        auto grace = text (b, "GraceNotes");
        if (grace == "BeforeBeat") beat.grace = GraceType::BeforeBeat;
        else if (grace == "OnBeat") beat.grace = GraceType::OnBeat;

        auto trem = text (b, "Tremolo");
        if (trem.isNotEmpty())
        {
            int den = trem.fromFirstOccurrenceOf ("/", false, false).getIntValue();
            if (den > 0) beat.tremoloTicks = std::max (PPQ / 16, PPQ * 4 / den);
        }
        beat.arpeggio = strokeFrom (text (b, "Arpeggio"));
        beat.brush = strokeFrom (propertyText (b, "Brush", "Direction"));
        beat.pickStroke = strokeFrom (propertyText (b, "PickStroke", "Direction"));
        beat.rasgueado = property (b, "Rasgueado") != nullptr;
        beat.slap = property (b, "Slapped") != nullptr;
        beat.pop  = property (b, "Popped") != nullptr;
        if (property (b, "WhammyBar") != nullptr)
            beat.whammy = readBendCurve (b, "WhammyBar");

        if (auto* xp = child (b, "XProperties"))
            for (auto* x : xp->getChildWithTagNameIterator ("XProperty"))
                if (x->getStringAttribute ("id") == "687935489")
                    beat.brushTicks = std::clamp (text (x, "Int").getIntValue(), 10, PPQ);
        if (beat.brush != Stroke::None && beat.brushTicks <= 0) beat.brushTicks = PPQ / 8;

        for (int nid : ints (text (b, "Notes")))
            if (auto* n = find (notes, nid))
                beat.notes.push_back (readNote (n, trackIndex));

        beat.rest = beat.notes.empty();
        return beat;
    }

    Note readNote (const XmlElement* n, int trackIndex)
    {
        auto& track = score->tracks[(size_t) trackIndex];
        auto& info  = infos[(size_t) trackIndex];
        Note note;

        if (auto* tie = child (n, "Tie"))
        {
            note.tieOrigin = tie->getBoolAttribute ("origin");
            note.tieDest   = tie->getBoolAttribute ("destination");
        }
        note.letRing = child (n, "LetRing") != nullptr;
        auto vib = text (n, "Vibrato");
        if (vib == "Slight") note.vibrato = 1;
        else if (vib == "Wide") note.vibrato = 2;
        note.ghost = child (n, "AntiAccent") != nullptr;
        const int acc = text (n, "Accent").getIntValue();
        note.staccato    = (acc & 0x01) != 0;
        note.heavyAccent = (acc & 0x04) != 0;
        note.accent      = (acc & 0x08) != 0;
        note.tenuto      = (acc & 0x10) != 0;

        note.palmMute   = property (n, "PalmMuted") != nullptr;
        note.dead       = property (n, "Muted") != nullptr;
        note.tapped     = property (n, "Tapped") != nullptr || property (n, "LeftHandTapped") != nullptr;
        note.hopoOrigin = property (n, "HopoOrigin") != nullptr;
        note.hopoDest   = property (n, "HopoDestination") != nullptr;
        if (auto* sl = property (n, "Slide")) note.slide = (uint8_t) text (sl, "Flags").getIntValue();
        if (property (n, "Bended") != nullptr) note.bend = readBendCurve (n, "Bend");

        auto ht = propertyText (n, "HarmonicType", "HType");
        if (ht.isNotEmpty())
        {
            if (ht == "Natural") note.harmonic = Harmonic::Natural;
            else if (ht == "Artificial") note.harmonic = Harmonic::Artificial;
            else if (ht == "Pinch") note.harmonic = Harmonic::Pinch;
            else if (ht == "Tap") note.harmonic = Harmonic::Tapped;
            else if (ht == "Semi") note.harmonic = Harmonic::Semi;
            else if (ht == "Feedback") note.harmonic = Harmonic::Feedback;
            note.harmonicFret = propertyText (n, "HarmonicFret", "HFret").getFloatValue();
        }

        const String trill = text (n, "Trill");
        if (trill.isNotEmpty()) { note.trillMidi = trill.getIntValue(); note.trillTicks = PPQ / 8; }

        if (track.percussion)
        {
            int midi = -1;
            auto art = propertyText (n, "InstrumentArticulation", "InstrumentArticulation");
            if (art.isNotEmpty() && ! info.drumArticulations.empty())
            {
                const int idx = art.getIntValue();
                if (idx >= 0 && idx < (int) info.drumArticulations.size()) midi = info.drumArticulations[(size_t) idx];
            }
            if (midi < 0 && hasPropertyField (n, "Midi", "Number")) midi = propertyText (n, "Midi", "Number").getIntValue();
            if (midi < 0 && property (n, "Element") != nullptr)
                midi = gp6DrumToMidi (propertyText (n, "Element", "Element").getIntValue(),
                                      propertyText (n, "Variation", "Variation").getIntValue());
            if (midi < 0) midi = 38;
            note.midi = normaliseDrumMidi (midi);
            note.string = -1;
            return note;
        }

        if (property (n, "String") != nullptr && property (n, "Fret") != nullptr && ! track.tuning.empty())
        {
            note.string = std::clamp (propertyText (n, "String", "String").getIntValue(), 0, (int) track.tuning.size() - 1);
            note.fret   = propertyText (n, "Fret", "Fret").getIntValue();
            note.midi   = track.tuning[(size_t) note.string] + track.capo + note.fret;
        }
        else if (hasPropertyField (n, "Midi", "Number"))
            note.midi = propertyText (n, "Midi", "Number").getIntValue();
        else if (auto* cp = property (n, "ConcertPitch"))
        {
            auto* p = child (cp, "Pitch");
            note.midi = (text (p, "Octave").getIntValue() + 1) * 12 + stepToSemitone (text (p, "Step")) + accidentalToSemitone (text (p, "Accidental"));
        }
        else if (property (n, "Tone") != nullptr && property (n, "Octave") != nullptr)
            note.midi = propertyText (n, "Octave", "Number").getIntValue() * 12 + propertyText (n, "Tone", "Step").getIntValue();

        note.midi = std::clamp (note.midi, 0, 127);
        return note;
    }
};
} // namespace

std::unique_ptr<Score> readGPIF (const juce::String& xml, juce::String& error)
{
    GPIFParser p;
    return p.parse (xml, error);
}

// ---------------------------------------------------------------------------------------------
// GPX (Guitar Pro 6) container: BCFZ compression + BCFS sector file system

namespace
{
class BitReader
{
public:
    BitReader (const uint8_t* d, size_t n) : data (d), size (n) {}

    int readBit()
    {
        if (bytePos >= size) throw std::runtime_error ("eof");
        int bit = (data[bytePos] >> (7 - bitPos)) & 1;
        if (++bitPos == 8) { bitPos = 0; ++bytePos; }
        return bit;
    }
    int readBits (int n) { int v = 0; for (int i = 0; i < n; ++i) v = (v << 1) | readBit(); return v; }
    int readBitsReversed (int n) { int v = 0; for (int i = 0; i < n; ++i) v |= readBit() << i; return v; }
    int readByte() { return readBits (8); }

private:
    const uint8_t* data;
    size_t size, bytePos = 0;
    int bitPos = 0;
};

int32_t le32 (const uint8_t* p) { return (int32_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24)); }

std::vector<uint8_t> bcfzDecompress (const uint8_t* d, size_t n)
{
    BitReader br (d, n);
    std::vector<uint8_t> out;
    int expected = 0;
    for (int i = 0; i < 4; ++i) expected |= br.readByte() << (8 * i);
    if (expected <= 0 || expected > 64 * 1024 * 1024) return out;
    out.reserve ((size_t) expected);
    try
    {
        while ((int) out.size() < expected)
        {
            if (br.readBits (1) == 1)
            {
                const int wordSize = br.readBits (4);
                const int offset = br.readBitsReversed (wordSize);
                const int length = br.readBitsReversed (wordSize);
                const int src = (int) out.size() - offset;
                const int count = std::min (offset, length);
                if (src < 0 || offset <= 0) break;
                for (int i = 0; i < count; ++i) out.push_back (out[(size_t) (src + i)]);
            }
            else
            {
                const int len = br.readBitsReversed (2);
                for (int i = 0; i < len; ++i) out.push_back ((uint8_t) br.readByte());
            }
        }
    }
    catch (...) {}
    return out;
}
} // namespace

bool decodeGPXContainer (const uint8_t* data, size_t size, juce::String& gpifOut, juce::String& error)
{
    if (size < 8) { error = "file too small"; return false; }
    std::vector<uint8_t> fs;
    if (std::memcmp (data, "BCFZ", 4) == 0) fs = bcfzDecompress (data + 4, size - 4);
    else if (std::memcmp (data, "BCFS", 4) == 0) fs.assign (data, data + size);
    else { error = "not a GPX container"; return false; }

    if (fs.size() < 8 || std::memcmp (fs.data(), "BCFS", 4) != 0) { error = "corrupt GPX container"; return false; }

    const uint8_t* fsData = fs.data() + 4;
    const size_t fsSize = fs.size() - 4;
    const size_t sector = 0x1000;

    for (size_t offset = sector; offset + 3 < fsSize; offset += sector)
    {
        if (le32 (fsData + offset) != 2) continue;
        if (offset + 0x94 > fsSize) break;
        std::string name ((const char*) fsData + offset + 4, strnlen ((const char*) fsData + offset + 4, 127));
        const int fileSize = le32 (fsData + offset + 0x8c);
        std::vector<uint8_t> content;
        size_t ptr = offset + 0x94;
        size_t lastSectorOffset = offset;
        while (ptr + 4 <= fsSize)
        {
            const int s = le32 (fsData + ptr);
            ptr += 4;
            if (s == 0) break;
            const size_t so = (size_t) s * sector;
            if (so + sector > fsSize) break;
            content.insert (content.end(), fsData + so, fsData + so + sector);
            lastSectorOffset = so;
        }
        if (name == "score.gpif")
        {
            content.resize ((size_t) std::clamp (fileSize, 0, (int) content.size()));
            gpifOut = juce::String::fromUTF8 ((const char*) content.data(), (int) content.size());
            return true;
        }
        offset = lastSectorOffset;
    }
    error = "score.gpif not found in GPX";
    return false;
}

// ---------------------------------------------------------------------------------------------

std::unique_ptr<Score> loadGuitarPro (const void* data, size_t size, juce::String& error)
{
    auto* bytes = static_cast<const uint8_t*> (data);
    if (size < 16) { error = "file too small"; return nullptr; }

    if (std::memcmp (bytes, "PK", 2) == 0)
    {
        juce::MemoryInputStream mis (data, size, false);
        juce::ZipFile zip (mis);
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* entry = zip.getEntry (i);
            if (entry->filename.endsWithIgnoreCase ("score.gpif"))
            {
                std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (i));
                if (! s) break;
                auto xml = s->readEntireStreamAsString();
                auto score = readGPIF (xml, error);
                if (score) score->formatName = "Guitar Pro 7/8";
                return score;
            }
        }
        error = "score.gpif not found in .gp archive";
        return nullptr;
    }

    if (std::memcmp (bytes, "BCFZ", 4) == 0 || std::memcmp (bytes, "BCFS", 4) == 0)
    {
        juce::String xml;
        if (! decodeGPXContainer (bytes, size, xml, error)) return nullptr;
        auto score = readGPIF (xml, error);
        if (score) score->formatName = "Guitar Pro 6";
        return score;
    }

    if (bytes[0] < 40 && std::memcmp (bytes + 1, "FICHIER GUITAR", 14) == 0)
        return readGP345 (bytes, size, error);

    // Raw GPIF XML
    juce::String head = juce::String::fromUTF8 ((const char*) bytes, (int) std::min<size_t> (size, 256));
    if (head.contains ("<GPIF"))
        return readGPIF (juce::String::fromUTF8 ((const char*) bytes, (int) size), error);

    error = "unrecognised file format";
    return nullptr;
}

std::unique_ptr<Score> loadGuitarPro (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) { error = "cannot read " + file.getFullPathName(); return nullptr; }
    auto s = loadGuitarPro (mb.getData(), mb.getSize(), error);
    if (s && s->title.empty()) s->title = file.getFileNameWithoutExtension().toStdString();
    return s;
}

bool isSupportedScoreFile (const juce::File& f)
{
    return f.hasFileExtension ("gp;gpx;gp5;gp4;gp3;gpif");
}
} // namespace tc
