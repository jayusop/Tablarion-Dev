// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — editor (front panel)
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/LcdDisplay.h"

#ifndef TABLARION_SOURCE_URL
 #define TABLARION_SOURCE_URL "(see README)"
#endif

class TablarionEditor;

class TrackRow final : public juce::Component
{
public:
    TrackRow (TablarionEditor& owner, TablarionProcessor& p, int index);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
    int index;

private:
    TablarionEditor& owner;
    TablarionProcessor& proc;
    juce::TextButton partBtn, muteBtn { "M" }, soloBtn { "S" };
    juce::Label name, family;
    juce::ComboBox engine;
    juce::Slider volume, pan;
    juce::Rectangle<int> meterArea;
};

class TablarionEditor final : public juce::AudioProcessorEditor,
                              public juce::FileDragAndDropTarget,
                              public juce::ChangeListener,
                              private juce::Timer
{
public:
    explicit TablarionEditor (TablarionProcessor&);
    ~TablarionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void mouseDown (const juce::MouseEvent&) override;
    void showAbout();

    void selectPart (int index);
    int getSelectedPart() const { return selectedPart; }

private:
    TablarionProcessor& proc;
    tcui::ModuleLookAndFeel lnf;
    tcui::LcdDisplay lcd;

    juce::TextButton openBtn { "OPEN" }, closeBtn { "CLOSE" }, rtzBtn { "|<" }, playBtn { "PLAY" }, loopBtn { "LOOP" }, syncBtn { "SYNC" },
                     strumBtn { "STRUM" }, exportBtn { "EXPORT WAV" }, sf2Btn { "LOAD SF2" };
    juce::ComboBox modeBox;
    juce::Slider volumeKnob, speedKnob, reverbKnob, humanizeKnob, position;
    juce::Label sf2Label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAtt, speedAtt, reverbAtt;

    juce::Viewport trackView;
    juce::Component trackHolder;
    juce::OwnedArray<TrackRow> rows;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };
    int selectedPart = 0;
    bool dragHover = false, draggingPosition = false;
    juce::Rectangle<int> lcdArea, brandArea, volumeArea, stripArea, mixerHeader, footerArea;
    juce::String toast;
    int toastTicks = 0;

    void timerCallback() override;
    void rebuildRows();
    void openScore();
    void openSoundFont();
    void exportWav();
    void loadFile (const juce::File& f);
    void showToast (const juce::String& s) { toast = s; toastTicks = 90; repaint(); }
    void drawScrew (juce::Graphics& g, float x, float y);
    int currentPlayIndex() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TablarionEditor)
};
