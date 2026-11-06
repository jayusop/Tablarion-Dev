// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Renders the plug-in editor to a PNG: UISnapshot <score> <out.png> [soundfont]
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    auto proc = std::make_unique<TablarionProcessor>();
    proc->prepareToPlay (48000.0, 512);
    if (argc > 1)
    {
        juce::String err;
        if (! proc->loadScoreFile (juce::File (juce::String::fromUTF8 (argv[1])), err)) printf ("load error: %s\n", err.toRawUTF8());
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    proc->play();
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    for (int i = 0; i < 1400; ++i) proc->processBlock (buf, midi); // ~15 s into the song
    if (argc > 3 && juce::String (argv[3]) == "--close")
    {
        proc->closeScore();
        float peak = 0.0f;
        for (int i = 0; i < 200; ++i) { proc->processBlock (buf, midi); peak = std::max (peak, buf.getMagnitude (0, 512)); }
        printf ("after close: hasScore %d playing %d peak(after tails) %.5f\n", (int) proc->hasScore(), (int) proc->isPlaying(), buf.getMagnitude (0, 512));
        juce::String err;
        const bool reopened = proc->loadScoreFile (juce::File (juce::String::fromUTF8 (argv[1])), err);
        printf ("reopen after close: %d\n", (int) reopened);
        proc->closeScore();
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File out (juce::String::fromUTF8 (argc > 2 ? argv[2] : "ui.png"));
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png; png.writeImageToStream (img, os);
    printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    ed.reset();
    return 0;
}
