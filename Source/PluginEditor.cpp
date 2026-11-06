// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
#include "PluginEditor.h"

using namespace tcui;

namespace
{
constexpr int kWidth = 1040, kHeight = 690, kRowH = 30;

juce::String formatTime (double sec)
{
    const int s = (int) sec;
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

void styleKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
}
} // namespace

// ================================================================================================

TrackRow::TrackRow (TablarionEditor& o, TablarionProcessor& p, int i) : index (i), owner (o), proc (p)
{
    partBtn.setButtonText (juce::String (i + 1).paddedLeft ('0', 2));
    partBtn.setTooltip ("Select this part (shown on the LCD and played by MIDI input)");
    partBtn.onClick = [this] { owner.selectPart (index); };
    addAndMakeVisible (partBtn);

    name.setFont (juce::Font (juce::FontOptions (13.5f).withStyle ("Bold")));
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);
    family.setFont (juce::Font (juce::FontOptions (12.0f)));
    family.setColour (juce::Label::textColourId, Colours::silkDim);
    family.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (family);

    engine.addItem ("Auto", 1);
    engine.addItem ("Modeled", 2);
    engine.addItem ("SoundFont", 3);
    engine.setTooltip ("Sound engine for this track");
    engine.onChange = [this]
    {
        const auto e = (tc::TrackEngine) (engine.getSelectedId() - 1);
        if ((int) e != proc.controls.trackEngine[index].load()) proc.setTrackEngine (index, e);
    };
    addAndMakeVisible (engine);

    muteBtn.setClickingTogglesState (true);
    soloBtn.setClickingTogglesState (true);
    muteBtn.onClick = [this] { proc.controls.mute[index] = muteBtn.getToggleState(); };
    soloBtn.onClick = [this] { proc.controls.solo[index] = soloBtn.getToggleState(); };
    addAndMakeVisible (muteBtn);
    addAndMakeVisible (soloBtn);

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.0);
    volume.onValueChange = [this] { proc.controls.volume[index] = (float) volume.getValue(); };
    addAndMakeVisible (volume);

    styleKnob (pan);
    pan.setRange (0.0, 1.0);
    pan.setDoubleClickReturnValue (true, 0.5);
    pan.onValueChange = [this] { proc.controls.pan[index] = (float) pan.getValue(); };
    addAndMakeVisible (pan);

    refresh();
}

void TrackRow::refresh()
{
    auto sc = proc.getScore();
    if (sc && index < (int) sc->tracks.size())
    {
        const auto& t = sc->tracks[(size_t) index];
        name.setText (juce::String::fromUTF8 (t.name.c_str()).isEmpty() ? "Track " + juce::String (index + 1) : juce::String::fromUTF8 (t.name.c_str()), juce::dontSendNotification);
        juce::String fam = tc::familyName (t.family);
        if (! t.percussion && ! t.tuning.empty() && tc::isStringModelFamily (t.family)) fam << "  " << (int) t.tuning.size() << "str";
        family.setText (fam, juce::dontSendNotification);
    }
    engine.setSelectedId (proc.controls.trackEngine[index].load() + 1, juce::dontSendNotification);
    muteBtn.setToggleState (proc.controls.mute[index].load(), juce::dontSendNotification);
    soloBtn.setToggleState (proc.controls.solo[index].load(), juce::dontSendNotification);
    volume.setValue (proc.controls.volume[index].load(), juce::dontSendNotification);
    pan.setValue (proc.controls.pan[index].load(), juce::dontSendNotification);
    engine.setTooltip ("Current engine: " + proc.getEngineName (index));
}

void TrackRow::resized()
{
    auto r = getLocalBounds().reduced (6, 3);
    partBtn.setBounds (r.removeFromLeft (38)); r.removeFromLeft (8);
    name.setBounds (r.removeFromLeft (190));
    family.setBounds (r.removeFromLeft (170));
    engine.setBounds (r.removeFromLeft (110).reduced (0, 1)); r.removeFromLeft (10);
    muteBtn.setBounds (r.removeFromLeft (28)); r.removeFromLeft (4);
    soloBtn.setBounds (r.removeFromLeft (28)); r.removeFromLeft (14);
    volume.setBounds (r.removeFromLeft (200)); r.removeFromLeft (10);
    pan.setBounds (r.removeFromLeft (32).expanded (0, 4)); r.removeFromLeft (14);
    meterArea = r.reduced (0, 7);
}

void TrackRow::paint (juce::Graphics& g)
{
    const bool sel = owner.getSelectedPart() == index;
    auto r = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
    g.setColour (sel ? Colours::amber.withAlpha (0.10f) : (index % 2 ? juce::Colour (0xff1b1c20) : juce::Colour (0xff1f2025)));
    g.fillRoundedRectangle (r, 4.0f);
    if (sel)
    {
        g.setColour (Colours::amber.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    // meter: segmented
    if (! meterArea.isEmpty())
    {
        const float lv = juce::jlimit (0.0f, 1.0f, std::sqrt (proc.controls.meter[index].load()) * 1.1f);
        const int segs = 14;
        const float sw = (float) meterArea.getWidth() / (float) segs;
        for (int s = 0; s < segs; ++s)
        {
            const bool lit = (float) s / (float) segs < lv;
            juce::Colour c = s >= segs - 2 ? Colours::ledRed : (s >= segs - 5 ? Colours::amber : Colours::accent);
            g.setColour (lit ? c : c.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> ((float) meterArea.getX() + s * sw + 1.0f, (float) meterArea.getY(), sw - 2.0f, (float) meterArea.getHeight()));
        }
    }
}

void TrackRow::mouseDown (const juce::MouseEvent&) { owner.selectPart (index); }

// ================================================================================================

TablarionEditor::TablarionEditor (TablarionProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (kWidth, kHeight);
    addAndMakeVisible (lcd);

    for (auto* b : { &openBtn, &closeBtn, &rtzBtn, &playBtn, &loopBtn, &syncBtn, &strumBtn, &exportBtn, &sf2Btn })
        addAndMakeVisible (*b);

    openBtn.setTooltip ("Open a Guitar Pro file (.gp, .gpx, .gp5, .gp4, .gp3)");
    closeBtn.setTooltip ("Close the current Guitar Pro file");
    rtzBtn.setTooltip ("Return to start");
    playBtn.setTooltip ("Play / Stop (internal transport)");
    loopBtn.setTooltip ("Loop the song");
    syncBtn.setTooltip ("Follow the host transport and tempo (DAW sync)");
    strumBtn.setTooltip ("Natural strumming of guitar chords");
    exportBtn.setTooltip ("Render the song to a 24-bit WAV file");
    sf2Btn.setTooltip ("Load a SoundFont 2 bank for sampled instruments");

    loopBtn.setClickingTogglesState (true);
    syncBtn.setClickingTogglesState (true);
    strumBtn.setClickingTogglesState (true);
    loopBtn.setToggleState (proc.controls.loop.load(), juce::dontSendNotification);
    syncBtn.setToggleState (proc.followHost.load(), juce::dontSendNotification);
    strumBtn.setToggleState (proc.getAutoStrum(), juce::dontSendNotification);

    openBtn.onClick = [this] { openScore(); };
    closeBtn.onClick = [this] { proc.closeScore(); selectPart (0); };
    rtzBtn.onClick = [this] { proc.seekTick (0); };
    playBtn.onClick = [this] { proc.togglePlay(); };
    loopBtn.onClick = [this] { proc.controls.loop = loopBtn.getToggleState(); };
    syncBtn.onClick = [this] { proc.followHost = syncBtn.getToggleState(); };
    strumBtn.onClick = [this] { proc.setAutoStrum (strumBtn.getToggleState()); };
    exportBtn.onClick = [this] { exportWav(); };
    sf2Btn.onClick = [this] { openSoundFont(); };

    modeBox.addItem ("Modeled (built-in)", 1);
    modeBox.addItem ("SoundFont", 2);
    modeBox.addItem ("Hybrid: modeled guitars + SF2", 3);
    modeBox.setTooltip ("Sound source: physical models, a SoundFont, or both");
    modeBox.onChange = [this]
    {
        const auto m = (tc::SoundMode) (modeBox.getSelectedId() - 1);
        if (m != tc::SoundMode::Modeled && ! proc.getSoundFont()) { showToast ("Load a SoundFont first"); modeBox.setSelectedId (1, juce::dontSendNotification); return; }
        if (m != proc.getSoundMode()) proc.setSoundMode (m);
    };
    addAndMakeVisible (modeBox);

    sf2Label.setFont (juce::Font (juce::FontOptions (11.5f)));
    sf2Label.setColour (juce::Label::textColourId, Colours::silkDim);
    addAndMakeVisible (sf2Label);

    for (auto* k : { &volumeKnob, &speedKnob, &reverbKnob, &humanizeKnob }) { styleKnob (*k); addAndMakeVisible (*k); }
    volumeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "master", volumeKnob);
    speedAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "speed", speedKnob);
    reverbAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "reverb", reverbKnob);
    speedKnob.setDoubleClickReturnValue (true, 1.0);
    reverbKnob.setDoubleClickReturnValue (true, 1.0);
    volumeKnob.setTooltip ("Master volume");
    speedKnob.setTooltip ("Playback speed (double-click: 100%)");
    reverbKnob.setTooltip ("Hall reverb amount");
    humanizeKnob.setTooltip ("Humanize: timing and velocity variation");
    humanizeKnob.setRange (0.0, 1.0);
    humanizeKnob.setValue (proc.getHumanize(), juce::dontSendNotification);
    humanizeKnob.onDragEnd = [this] { proc.setHumanize ((float) humanizeKnob.getValue()); };

    position.setSliderStyle (juce::Slider::LinearHorizontal);
    position.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    position.setRange (0.0, 1.0, 1.0);
    position.onDragStart = [this] { draggingPosition = true; };
    position.onDragEnd = [this] { draggingPosition = false; proc.seekBar ((int) position.getValue()); };
    position.onValueChange = [this] { if (! draggingPosition && position.isMouseButtonDown()) proc.seekBar ((int) position.getValue()); };
    addAndMakeVisible (position);

    trackView.setViewedComponent (&trackHolder, false);
    trackView.setScrollBarsShown (true, false);
    trackView.setScrollBarThickness (8);
    addAndMakeVisible (trackView);

    selectedPart = juce::jmax (0, proc.controls.liveTrack.load());
    proc.addChangeListener (this);
    rebuildRows();
    changeListenerCallback (nullptr);
    startTimerHz (30);
}

TablarionEditor::~TablarionEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void TablarionEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto sc = proc.getScore();
    const int nrows = sc ? std::min ((int) sc->tracks.size(), tc::kMaxTracks) : 0;
    if (nrows != rows.size()) rebuildRows();
    for (auto* r : rows) r->refresh();
    modeBox.setSelectedId ((int) proc.getSoundMode() + 1, juce::dontSendNotification);
    auto sf = proc.getSoundFont();
    sf2Label.setText (sf ? "SF2: " + juce::String (sf->name) : juce::String ("No SoundFont (modeled instruments)"), juce::dontSendNotification);
    strumBtn.setToggleState (proc.getAutoStrum(), juce::dontSendNotification);
    humanizeKnob.setValue (proc.getHumanize(), juce::dontSendNotification);
    if (auto song = proc.getSong()) position.setRange (0.0, juce::jmax (1.0, (double) song->bars.size() - 1.0), 1.0);
    else { position.setRange (0.0, 1.0, 1.0); position.setValue (0.0, juce::dontSendNotification); }
    const bool loaded = proc.hasScore();
    for (auto* b : { &closeBtn, &rtzBtn, &playBtn, &exportBtn }) b->setEnabled (loaded);
    position.setEnabled (loaded);
    if (proc.getStatus().isNotEmpty()) showToast (proc.getStatus());
    repaint();
}

void TablarionEditor::rebuildRows()
{
    rows.clear();
    auto sc = proc.getScore();
    const int n = sc ? std::min ((int) sc->tracks.size(), tc::kMaxTracks) : 0;
    for (int i = 0; i < n; ++i)
    {
        auto* r = rows.add (new TrackRow (*this, proc, i));
        trackHolder.addAndMakeVisible (r);
    }
    if (selectedPart >= n) selectedPart = 0;
    resized();
}

void TablarionEditor::selectPart (int index)
{
    selectedPart = index;
    proc.controls.liveTrack = index;
    for (auto* r : rows) r->repaint();
}

int TablarionEditor::currentPlayIndex() const
{
    auto song = proc.getSong();
    if (! song || song->bars.empty()) return 0;
    const int64_t t = proc.positionTick();
    auto it = std::upper_bound (song->bars.begin(), song->bars.end(), t, [] (int64_t v, const tc::BarMark& b) { return v < b.tick; });
    return (int) std::max<ptrdiff_t> (0, std::distance (song->bars.begin(), it) - 1);
}

void TablarionEditor::timerCallback()
{
    LcdData d;
    auto sc = proc.getScore();
    auto song = proc.getSong();
    d.hasScore = sc != nullptr;
    d.playing = proc.isPlaying();
    if (sc)
    {
        juce::String t = juce::String::fromUTF8 (sc->title.c_str());
        if (! sc->artist.empty()) t = juce::String::fromUTF8 (sc->artist.c_str()) + " - " + t;
        d.title = t;
        d.numParts = (int) sc->tracks.size();
        if (selectedPart < (int) sc->tracks.size())
        {
            const auto& tr = sc->tracks[(size_t) selectedPart];
            d.instrument = juce::String::fromUTF8 (tr.name.c_str()).isNotEmpty() ? juce::String::fromUTF8 (tr.name.c_str()) : juce::String (tc::familyName (tr.family));
            d.engine = proc.getEngineName (selectedPart) + " / " + tc::familyName (tr.family);
        }
        d.part = selectedPart + 1;
        d.level = (int) std::lround (proc.controls.volume[selectedPart].load() * 127.0f);
        d.pan = proc.controls.pan[selectedPart].load();
        for (int i = 0; i < 16; ++i) d.levels[i] = std::sqrt (proc.controls.meter[i].load()) * 1.15f;
    }
    if (song)
    {
        const int64_t tick = proc.positionTick();
        const int pi = currentPlayIndex();
        d.totalBars = (int) song->bars.size();
        d.bar = pi + 1;
        d.tempo = song->tempo.bpmAt (tick) * proc.params.getRawParameterValue ("speed")->load();
        d.time = formatTime (song->tempo.secondsAt (tick)) + "/" + formatTime (song->lengthSeconds);
        if (! draggingPosition && ! position.isMouseButtonDown()) position.setValue (pi, juce::dontSendNotification);
    }
    const float ep = proc.getExportProgress();
    d.status = ep >= 0.0f ? "EXPORTING " + juce::String ((int) (ep * 100.0f)) + "%" : (toastTicks > 0 ? toast : (sc ? juce::String (sc->formatName) : juce::String ("READY")));
    lcd.setData (d);

    playBtn.setButtonText (d.playing ? "STOP" : "PLAY");
    if (toastTicks > 0) --toastTicks;
    for (auto* r : rows) r->repaint();
}

// ------------------------------------------------------------------------------------------------

void TablarionEditor::drawScrew (juce::Graphics& g, float x, float y)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a8e96), x - 5, y - 5, juce::Colour (0xff2c2e33), x + 5, y + 5, true));
    g.fillEllipse (x - 5, y - 5, 10, 10);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (x - 3.5f, y + 1.0f, x + 3.5f, y - 1.0f, 1.4f);
}

void TablarionEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (Colours::panelTop, 0, 0, Colours::panelBot, 0, r.getHeight(), false));
    g.fillAll();

    // front plate (upper module face)
    auto face = juce::Rectangle<float> (10, 10, r.getWidth() - 20, (float) stripArea.getBottom() - 4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff34363c), face.getX(), face.getY(), juce::Colour (0xff1e1f23), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 10.0f);
    // brushed texture
    juce::Random rng (7);
    for (int y = (int) face.getY() + 2; y < (int) face.getBottom() - 2; y += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (0.012f + rng.nextFloat() * 0.012f));
        g.drawHorizontalLine (y, face.getX() + 6, face.getRight() - 6);
    }
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (face.getX() + 10, face.getY() + 1, face.getRight() - 10, face.getY() + 1, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (face, 10.0f, 1.2f);
    drawScrew (g, face.getX() + 14, face.getY() + 14);
    drawScrew (g, face.getRight() - 14, face.getY() + 14);
    drawScrew (g, face.getX() + 14, face.getBottom() - 14);
    drawScrew (g, face.getRight() - 14, face.getBottom() - 14);

    // brand
    auto b = brandArea;
    {
        // two-tone wordmark: "Tab" in amber, "larion" in silkscreen grey
        const juce::Font logo (juce::FontOptions (33.0f).withStyle ("Bold"));
        auto logoRow = b.removeFromTop (40);
        const int tabW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (logo, "Tab"));
        g.setFont (logo);
        g.setColour (Colours::amber);
        g.drawText ("Tab", logoRow.removeFromLeft (tabW), juce::Justification::left);
        g.setColour (Colours::silk);
        g.drawText ("larion", logoRow, juce::Justification::left);
    }
    auto line = b.removeFromTop (24);
    g.setColour (Colours::accent);
    g.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
    g.drawText ("INSTRUMENT PLUG-IN", line, juce::Justification::centredLeft);
    g.setColour (Colours::silkDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
    g.drawText ("TABLATURE SOUND MODULE  -  (i) ABOUT", b.removeFromTop (16), juce::Justification::left);
    g.setColour (Colours::amber.withAlpha (0.6f));
    g.fillRect (brandArea.getX(), brandArea.getY() + 84, 200, 2);
    g.setColour (Colours::silkDim);
    g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Bold")));
    g.drawText ("SOUND SOURCE", brandArea.getX(), modeBox.getY() - 14, 200, 12, juce::Justification::left);

    // power LED
    const bool on = proc.isPlaying();
    auto led = juce::Rectangle<float> ((float) brandArea.getRight() - 14, (float) brandArea.getY() + 12, 8, 8);
    g.setColour ((on ? Colours::ledGreen : Colours::ledGreen.withAlpha (0.35f)).withAlpha (on ? 0.35f : 0.0f));
    g.fillEllipse (led.expanded (4));
    g.setColour (on ? Colours::ledGreen : juce::Colour (0xff1d3a22));
    g.fillEllipse (led);

    // knob labels (silkscreen)
    g.setColour (Colours::silk);
    g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    g.drawText ("VOLUME", volumeKnob.getBounds().translated (0, volumeKnob.getHeight() - 6).withHeight (14), juce::Justification::centred);
    g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Bold")));
    g.setColour (Colours::silkDim);
    for (auto* k : { &speedKnob, &reverbKnob, &humanizeKnob })
    {
        const char* label = k == &speedKnob ? "SPEED" : (k == &reverbKnob ? "REVERB" : "HUMANIZE");
        g.drawText (label, k->getBounds().translated (0, k->getHeight() - 4).withHeight (12).expanded (12, 0), juce::Justification::centred);
    }
    g.drawText ("POSITION", position.getX(), position.getY() - 13, 80, 12, juce::Justification::left);

    // mixer header
    g.setColour (Colours::silkDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
    auto h = mixerHeader.reduced (14, 0);
    const int cols[] = { 46, 190, 170, 120, 32, 42, 210, 46 };
    const char* names[] = { "PART", "TRACK", "INSTRUMENT", "ENGINE", "M", "S", "VOLUME", "PAN" };
    for (int i = 0; i < 8; ++i) g.drawText (names[i], h.removeFromLeft (cols[i]), juce::Justification::left);
    g.drawText ("LEVEL", h, juce::Justification::left);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (trackView.getBounds().toFloat().expanded (4.0f), 6.0f);

    // footer
    g.setColour (Colours::silkDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Drop .gp  .gpx  .gp5  .gp4  .gp3  or a .sf2 SoundFont anywhere on the panel   |   MIDI input plays the selected part",
                footerArea, juce::Justification::centredLeft);
    g.setColour (Colours::accent.withAlpha (0.8f));
    g.drawText ("Free software - AGPLv3 - click the logo for licence info", footerArea, juce::Justification::centredRight);

    if (rows.isEmpty())
    {
        g.setColour (Colours::silkDim);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText ("No score loaded  -  click OPEN or drop a Guitar Pro file here", trackView.getBounds(), juce::Justification::centred);
    }

    if (dragHover)
    {
        g.setColour (Colours::amber.withAlpha (0.12f));
        g.fillAll();
        g.setColour (Colours::amber);
        g.drawRect (getLocalBounds().reduced (3), 3);
    }
}

void TablarionEditor::mouseDown (const juce::MouseEvent& e)
{
    if (brandArea.contains (e.getPosition())) showAbout();
}

void TablarionEditor::showAbout()
{
    juce::String msg;
    msg << "Tablarion  " << JucePlugin_VersionString << "\n"
        << "Copyright (C) 2026 the Tablarion contributors\n\n"
        << "This program is free software: you can redistribute it and/or modify it under the terms of the "
           "GNU Affero General Public License version 3 or later. It comes with ABSOLUTELY NO WARRANTY.\n"
        << "Source code: " << TABLARION_SOURCE_URL << "\n\n"
        << "Built with JUCE (AGPLv3) and the Steinberg VST3 SDK (GPLv3). See THIRD_PARTY_NOTICES in the bundle.\n\n"
        << "Trademarks: VST is a registered trademark of Steinberg Media Technologies GmbH. Audio Units is a trademark "
           "of Apple Inc. Guitar Pro is a trademark of Arobas Music. SoundFont is a registered trademark of Creative "
           "Technology Ltd. All other trademarks belong to their owners. Tablarion is an independent project and is not "
           "affiliated with, endorsed by or sponsored by any of these companies. Guitar Pro file support is provided "
           "for interoperability only; no Guitar Pro software, code or sound data is included.";
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About Tablarion", msg, "OK", this);
}

void TablarionEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (232);
    brandArea = top.removeFromLeft (250).reduced (24, 20);
    volumeArea = top.removeFromRight (190);
    lcdArea = top.reduced (8, 16);
    auto lcdBox = lcdArea.withTrimmedBottom (36);
    lcd.setBounds (lcdBox);
    position.setBounds (lcdArea.getX() + 6, lcdBox.getBottom() + 16, lcdArea.getWidth() - 12, 20);

    auto bb = brandArea;
    bb.removeFromTop (106);
    modeBox.setBounds (bb.removeFromTop (24).withWidth (205));
    bb.removeFromTop (8);
    sf2Btn.setBounds (bb.removeFromTop (26).withWidth (100));
    sf2Label.setBounds (brandArea.getX() - 2, sf2Btn.getBottom() + 2, 230, 18);

    volumeKnob.setBounds (volumeArea.withSizeKeepingCentre (130, 130).translated (-8, -6));

    stripArea = r.removeFromTop (70);
    auto s = stripArea.reduced (24, 14);
    for (auto* btn : { &openBtn, &closeBtn, &rtzBtn, &playBtn, &loopBtn, &syncBtn, &strumBtn, &exportBtn })
    {
        const int w = btn == &exportBtn ? 112 : (btn == &rtzBtn ? 48 : (btn == &openBtn || btn == &closeBtn ? 74 : 80));
        btn->setBounds (s.removeFromLeft (w).withHeight (32));
        s.removeFromLeft (8);
    }
    auto knobs = s.removeFromRight (230);
    for (auto* k : { &speedKnob, &reverbKnob, &humanizeKnob })
        k->setBounds (knobs.removeFromLeft (76).withSizeKeepingCentre (46, 46).translated (0, -6));

    r.removeFromTop (8);
    footerArea = r.removeFromBottom (24).reduced (14, 0);
    mixerHeader = r.removeFromTop (18);
    r.removeFromTop (2);
    trackView.setBounds (r.reduced (8, 4));
    trackHolder.setSize (trackView.getWidth() - 10, juce::jmax (trackView.getHeight(), rows.size() * kRowH));
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * kRowH, trackHolder.getWidth(), kRowH);
}

// ------------------------------------------------------------------------------------------------

bool TablarionEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        juce::File file (f);
        if (tc::isSupportedScoreFile (file) || file.hasFileExtension ("sf2")) return true;
    }
    return false;
}

void TablarionEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    for (auto& f : files) loadFile (juce::File (f));
    repaint();
}

void TablarionEditor::loadFile (const juce::File& f)
{
    if (f.hasFileExtension ("sf2")) { proc.loadSoundFontAsync (f); return; }
    juce::String err;
    if (! proc.loadScoreFile (f, err)) showToast ("ERROR: " + err);
    else selectPart (0);
}

void TablarionEditor::openScore()
{
    chooser = std::make_unique<juce::FileChooser> ("Open Guitar Pro file", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.gp;*.gpx;*.gp5;*.gp4;*.gp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
}

void TablarionEditor::openSoundFont()
{
    juce::PopupMenu m;
    m.addItem (1, "Load SoundFont (.sf2)...");
    auto cands = proc.getRecentSoundFontCandidates();
    for (int i = 0; i < cands.size(); ++i) m.addItem (100 + i, juce::File (cands[i]).getFileName());
    m.addSeparator();
    m.addItem (2, "Unload SoundFont", proc.getSoundFont() != nullptr);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sf2Btn), [this, cands] (int id)
    {
        if (id == 1)
        {
            chooser = std::make_unique<juce::FileChooser> ("Load SoundFont", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.sf2");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadSoundFontAsync (fc.getResult()); });
        }
        else if (id == 2) proc.clearSoundFont();
        else if (id >= 100) proc.loadSoundFontAsync (juce::File (cands[id - 100]));
    });
}

void TablarionEditor::exportWav()
{
    auto sc = proc.getScore();
    if (! sc) { showToast ("Load a score first"); return; }
    if (proc.getExportProgress() >= 0.0f) { showToast ("Export already running"); return; }
    juce::String name = juce::String::fromUTF8 (sc->title.c_str());
    if (name.isEmpty()) name = "Tablarion Export";
    auto def = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (juce::File::createLegalFileName (name) + ".wav");
    chooser = std::make_unique<juce::FileChooser> ("Export WAV", def, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
        juce::Component::SafePointer<TablarionEditor> safe (this);
        proc.exportWavAsync (f, 48000.0, [safe] (bool ok, juce::String msg)
        {
            if (safe) safe->showToast (ok ? msg : "ERROR: " + msg);
        });
    });
}
