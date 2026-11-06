// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — Guitar Pro file readers
#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include "../Model/Score.h"

namespace tc
{
/** Loads .gp3 .gp4 .gp5 .gpx .gp files (format detected from content). Returns nullptr on failure. */
std::unique_ptr<Score> loadGuitarPro (const void* data, size_t size, juce::String& error);
std::unique_ptr<Score> loadGuitarPro (const juce::File& file, juce::String& error);

// Individual readers (exposed for tests)
std::unique_ptr<Score> readGP345 (const uint8_t* data, size_t size, juce::String& error);
std::unique_ptr<Score> readGPIF (const juce::String& xml, juce::String& error);
bool decodeGPXContainer (const uint8_t* data, size_t size, juce::String& gpifOut, juce::String& error);

bool isSupportedScoreFile (const juce::File& f);
} // namespace tc
