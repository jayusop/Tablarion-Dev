// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — hardware-module look & feel (half-rack sound module aesthetic)
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tcui
{
namespace Colours
{
    const juce::Colour panelTop   { 0xff2a2c31 };
    const juce::Colour panelBot   { 0xff16171a };
    const juce::Colour bezel      { 0xff0c0d0f };
    const juce::Colour silk       { 0xffc9ccd2 }; // silkscreen text
    const juce::Colour silkDim    { 0xff7d828c };
    const juce::Colour amber      { 0xffffa53a };
    const juce::Colour amberDim   { 0x33ffa53a };
    const juce::Colour lcdGlass   { 0xff120b05 };
    const juce::Colour accent     { 0xff3fd0c9 }; // plug-in "signal" teal
    const juce::Colour ledRed     { 0xffff4d3d };
    const juce::Colour ledGreen   { 0xff59f07a };
}

class ModuleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModuleLookAndFeel()
    {
        setColour (juce::Label::textColourId, Colours::silk);
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1d1f23));
        setColour (juce::ComboBox::textColourId, Colours::silk);
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3d44));
        setColour (juce::ComboBox::arrowColourId, Colours::amber);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d1f23));
        setColour (juce::PopupMenu::textColourId, Colours::silk);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::amber.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextButton::textColourOffId, Colours::silk);
        setColour (juce::TextButton::textColourOnId, Colours::amber);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff4a4d55));
        setColour (juce::Slider::textBoxTextColourId, Colours::silk);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff24262b));
        setColour (juce::TooltipWindow::textColourId, Colours::silk);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions ((float) juce::jmin (13, h - 8)).withStyle ("Bold"));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float cr = 4.0f;
        // drop shadow
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), cr);
        // rubber cap
        juce::Colour top (0xff474a52), bot (0xff2b2d33);
        if (over) { top = top.brighter (0.12f); bot = bot.brighter (0.08f); }
        if (down) { std::swap (top, bot); }
        if (! b.isEnabled()) { top = top.darker (0.25f); bot = bot.darker (0.25f); }
        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, cr);
        g.setColour (juce::Colours::white.withAlpha (down ? 0.03f : 0.10f));
        g.drawLine (r.getX() + cr, r.getY() + 1.0f, r.getRight() - cr, r.getY() + 1.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r, cr, 1.0f);

        if (b.getClickingTogglesState() && b.getWidth() > 50)
        {
            // status LED like on hardware modules
            auto led = juce::Rectangle<float> (r.getX() + 6.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
            const auto on = b.getToggleState();
            if (on)
            {
                g.setColour (Colours::amber.withAlpha (0.35f));
                g.fillEllipse (led.expanded (3.0f));
            }
            g.setColour (on ? Colours::amber : juce::Colour (0xff3b2a17));
            g.fillEllipse (led);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        auto r = b.getLocalBounds();
        if (b.getClickingTogglesState() && b.getWidth() > 50) r.removeFromLeft (12);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        juce::Colour c = b.getToggleState() && b.getClickingTogglesState() ? Colours::amber : Colours::silk;
        if (! b.isEnabled()) c = Colours::silkDim.withAlpha (0.45f);
        g.setColour (c);
        g.drawText (b.getButtonText(), r.translated (0, down ? 1 : 0), juce::Justification::centred);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);
        const auto c = r.getCentre();
        const float rad = size * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        // scale dots
        const int dots = 11;
        for (int i = 0; i < dots; ++i)
        {
            const float a = startAngle + (float) i / (float) (dots - 1) * (endAngle - startAngle);
            const float lit = (float) i / (float) (dots - 1) <= pos + 0.001f ? 1.0f : 0.0f;
            const auto p = c.getPointOnCircumference (rad, a);
            g.setColour (lit > 0 ? Colours::amber : Colours::silkDim.withAlpha (0.5f));
            g.fillEllipse (p.x - 1.3f, p.y - 1.3f, 2.6f, 2.6f);
        }

        auto knob = r.reduced (size * 0.16f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (knob.translated (0.0f, 2.0f));
        juce::ColourGradient body (juce::Colour (0xff5a5d66), knob.getX(), knob.getY(),
                                   juce::Colour (0xff1b1c20), knob.getRight(), knob.getBottom(), false);
        g.setGradientFill (body);
        g.fillEllipse (knob);
        // knurled edge
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        const float kr = knob.getWidth() * 0.5f;
        for (int i = 0; i < 36; ++i)
        {
            const float a = (float) i / 36.0f * juce::MathConstants<float>::twoPi;
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (kr * 0.86f, a), c.getPointOnCircumference (kr, a)), 1.0f);
        }
        auto cap = knob.reduced (kr * 0.18f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3c3f46), cap.getX(), cap.getY(),
                                                 juce::Colour (0xff222327), cap.getX(), cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (cap, 1.0f);

        juce::Path p;
        p.addRoundedRectangle (-1.5f, -kr * 0.82f, 3.0f, kr * 0.42f, 1.5f);
        g.setColour (s.isEnabled() ? Colours::amber : Colours::silkDim);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                           const juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, 0, 0, style, s); return; }
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
        g.setColour (juce::Colour (0xff0b0c0e));
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (Colours::amber.withAlpha (0.65f));
        g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);
        auto thumb = juce::Rectangle<float> (12.0f, juce::jmin (18.0f, r.getHeight() - 2.0f)).withCentre ({ sliderPos, r.getCentreY() });
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (thumb.translated (0, 1.5f), 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9dce2), thumb.getX(), thumb.getY(),
                                                 juce::Colour (0xff7d818a), thumb.getX(), thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine (thumb.getCentreX(), thumb.getY() + 3, thumb.getCentreX(), thumb.getBottom() - 3, 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour (Colours::amber);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.5f)); }
    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (13.0f)); }
};
} // namespace tcui
