// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — instrument interface
#pragma once

#include <memory>
#include "Events.h"
#include "../DSP/DSP.h"

namespace tc
{
class SoundFont;

class Instrument
{
public:
    virtual ~Instrument() = default;
    virtual void prepare (float sampleRate) = 0;
    /** secPerTick converts the event's tick durations into seconds at the current tempo/speed. */
    virtual void noteOn (const NoteEvent& e, double secPerTick) = 0;
    /** Live MIDI note-off (events flagged EvLive sustain until this is called). */
    virtual void noteOff (int midiKey) = 0;
    virtual void allNotesOff (bool immediate) = 0;
    /** Adds n samples into L/R. */
    virtual void render (float* L, float* R, int n) = 0;
    /** Live pitch-bend in semitones (MIDI input). */
    virtual void setPitchBend (float semis) { (void) semis; }
    virtual const char* engineName() const = 0;
};

/** Physically modelled / synthesised instrument for a track family. */
std::unique_ptr<Instrument> makeModeledInstrument (const Track& track);

/** SoundFont-based instrument (nullptr if the bank has no usable preset). */
std::unique_ptr<Instrument> makeSoundFontInstrument (std::shared_ptr<const SoundFont> sf, const Track& track);

// Individual factories
std::unique_ptr<Instrument> makeStringInstrument (const Track& track);
std::unique_ptr<Instrument> makePianoInstrument (Family f);
std::unique_ptr<Instrument> makeMalletInstrument (int program);
std::unique_ptr<Instrument> makeSynthInstrument (Family f, int program);
std::unique_ptr<Instrument> makeDrumInstrument();
} // namespace tc
