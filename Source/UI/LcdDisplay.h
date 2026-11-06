// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 the Tablarion contributors. See LICENSE.
// Tablarion — dot-matrix LCD with part info and 16-part level bar graph (sound-module style)
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"

namespace tcui
{
struct LcdData
{
    juce::String title, instrument, engine, status, time;
    int part = 1, level = 100, bar = 0, totalBars = 0, numParts = 0;
    float pan = 0.5f, levels[16] {};
    double tempo = 120.0;
    bool playing = false, hasScore = false;
};

class LcdDisplay : public juce::Component
{
public:
    static constexpr float kPitch = 3.0f; // dot-matrix texture pitch (cosmetic only)

    void setData (const LcdData& d)
    {
        data = d;
        ++frame;
        repaint();
    }

    void resized() override { buildTexture(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::bezel);
        g.fillRoundedRectangle (r, 8.0f);
        auto glass = r.reduced (6.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1108), glass.getX(), glass.getY(),
                                                 Colours::lcdGlass, glass.getX(), glass.getBottom(), false));
        g.fillRoundedRectangle (glass, 5.0f);

        const auto inner = getLocalBounds().reduced (14, 10);
        g.drawImageAt (ghost, inner.getX(), inner.getY());
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (inner);
            drawContent (g, inner.toFloat());
        }
        // a light dot-matrix grid over the text (keeps the look, keeps it legible)
        g.drawImageAt (gaps, inner.getX(), inner.getY());

        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), glass.getX(), glass.getY(),
                                                 juce::Colours::transparentWhite, glass.getX(), glass.getCentreY(), false));
        g.fillRoundedRectangle (glass.withHeight (glass.getHeight() * 0.45f), 5.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (glass, 5.0f, 1.5f);
    }

private:
    LcdData data;
    juce::Image ghost, gaps;
    int frame = 0;

    static juce::Font mono (float size, bool bold)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
    }

    void buildTexture()
    {
        const auto inner = getLocalBounds().reduced (14, 10);
        const int w = juce::jmax (1, inner.getWidth()), h = juce::jmax (1, inner.getHeight());
        ghost = juce::Image (juce::Image::ARGB, w, h, true);
        gaps  = juce::Image (juce::Image::ARGB, w, h, true);
        {
            juce::Graphics g (ghost);
            g.setColour (Colours::amber.withAlpha (0.05f));
            for (float y = 0; y < (float) h; y += kPitch)
                for (float x = 0; x < (float) w; x += kPitch)
                    g.fillRect (x, y, kPitch - 1.0f, kPitch - 1.0f);
        }
        {
            juce::Graphics g (gaps);
            g.setColour (Colours::lcdGlass.withAlpha (0.28f));
            for (float x = kPitch - 1.0f; x < (float) w; x += kPitch) g.fillRect (x, 0.0f, 0.7f, (float) h);
            for (float y = kPitch - 1.0f; y < (float) h; y += kPitch) g.fillRect (0.0f, y, (float) w, 0.7f);
        }
    }

    /** Draws text with a soft amber glow; returns the x position after the text. */
    float glowText (juce::Graphics& g, const juce::String& s, float x, float y, float w, float h, const juce::Font& f,
                    juce::Colour c, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setFont (f);
        g.setColour (c.withMultipliedAlpha (0.22f));
        g.drawText (s, juce::Rectangle<float> (x - 0.8f, y, w, h), j, true);
        g.drawText (s, juce::Rectangle<float> (x + 0.8f, y, w, h), j, true);
        g.setColour (c);
        g.drawText (s, juce::Rectangle<float> (x, y, w, h), j, true);
        return x + juce::GlyphArrangement::getStringWidth (f, s);
    }

    /** "LABEL value" pair; returns the end x. */
    float pair (juce::Graphics& g, const juce::String& label, const juce::String& value, float x, float y, float h,
                float maxW, float labelSize, float valueSize)
    {
        const auto lf = mono (labelSize, false);
        const auto vf = mono (valueSize, true);
        float cx = glowText (g, label, x, y, maxW, h, lf, Colours::amber.withAlpha (0.72f));
        cx += labelSize * 0.6f;
        return glowText (g, value, cx, y, juce::jmax (10.0f, x + maxW - cx), h, vf, Colours::amber);
    }

    void drawContent (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const float W = area.getWidth(), H = area.getHeight();
        const float x0 = area.getX(), y0 = area.getY();

        // ---- title line
        const float titleH = std::round (H * 0.18f);
        const float titleSize = juce::jlimit (12.0f, 20.0f, titleH * 0.82f);
        const auto tf = mono (titleSize, true);
        const auto sf = mono (titleSize * 0.8f, true);
        const juce::String stateText = data.playing ? ">PLAY" : "-STOP";
        const float stateW = juce::GlyphArrangement::getStringWidth (sf, stateText) + 8.0f;
        const float titleW = W - stateW - 10.0f;
        const juce::String title = data.hasScore ? data.title : juce::String ("Tablarion  -  drop a Guitar Pro file");
        const float tw = juce::GlyphArrangement::getStringWidth (tf, title);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (juce::Rectangle<float> (x0, y0, titleW, titleH).toNearestInt());
            if (tw > titleW)
            {
                const float span = tw + 60.0f;
                const float off = std::fmod ((float) frame * 1.5f, span);
                glowText (g, title, x0 - off, y0, tw + 4.0f, titleH, tf, Colours::amber);
                glowText (g, title, x0 - off + span, y0, tw + 4.0f, titleH, tf, Colours::amber);
            }
            else glowText (g, title, x0, y0, titleW, titleH, tf, Colours::amber);
        }
        glowText (g, stateText, x0 + W - stateW, y0, stateW, titleH, sf,
                  data.playing ? Colours::amber : Colours::amber.withAlpha (0.6f), juce::Justification::centredRight);

        g.setColour (Colours::amber.withAlpha (0.35f));
        g.fillRect (x0, y0 + titleH + 2.0f, W, 1.0f);

        // ---- left block: five rows
        const float split = std::round (W * 0.6f);
        const float top = y0 + titleH + 6.0f;
        const float rowH = (y0 + H - top) / 5.0f;
        const float labelSize = juce::jlimit (9.0f, 14.0f, rowH * 0.5f);
        const float valueSize = juce::jlimit (10.0f, 16.0f, rowH * 0.6f);
        const float gap = labelSize * 1.6f;

        float y = top;
        {
            float cx = pair (g, "PART", juce::String (data.part).paddedLeft ('0', 2), x0, y, rowH, split, labelSize, valueSize * 1.12f);
            glowText (g, data.instrument.toUpperCase(), cx + gap * 0.6f, y, split - (cx - x0) - gap * 0.6f, rowH,
                      mono (valueSize * 1.12f, true), Colours::amber);
        }
        y += rowH;
        {
            const int panv = (int) std::lround ((data.pan - 0.5f) * 128.0f);
            const juce::String pan = panv == 0 ? juce::String ("C") : (panv < 0 ? "L" + juce::String (-panv) : "R" + juce::String (panv));
            float cx = pair (g, "LEVEL", juce::String (data.level), x0, y, rowH, split, labelSize, valueSize);
            cx = pair (g, "PAN", pan, cx + gap, y, rowH, split - (cx - x0), labelSize, valueSize);
            pair (g, "TEMPO", juce::String ((int) std::lround (data.tempo)), cx + gap, y, rowH, x0 + split - (cx + gap), labelSize, valueSize);
        }
        y += rowH;
        {
            const juce::String bars = juce::String (data.bar).paddedLeft ('0', 3) + "/" + juce::String (data.totalBars).paddedLeft ('0', 3);
            float cx = pair (g, "BAR", bars, x0, y, rowH, split, labelSize, valueSize);
            pair (g, "TIME", data.time.isEmpty() ? juce::String ("-:--") : data.time, cx + gap, y, rowH, x0 + split - (cx + gap), labelSize, valueSize);
        }
        y += rowH;
        pair (g, "ENGINE", data.engine.isEmpty() ? juce::String ("-") : data.engine.toUpperCase(), x0, y, rowH, split - 4.0f, labelSize, valueSize * 0.92f);
        y += rowH;
        glowText (g, data.status, x0, y, split - 4.0f, rowH, mono (valueSize * 0.92f, false), Colours::amber.withAlpha (0.8f));

        // ---- right block: 16-part level bar graph
        const float gx = x0 + split + 10.0f, gw = x0 + W - gx;
        const float numSize = juce::jlimit (8.0f, 11.0f, labelSize * 0.85f);
        const float bottom = y0 + H - numSize - 4.0f;
        const float barTop = top + 2.0f;
        const float slot = gw / 16.0f;
        const float barW = juce::jmax (2.0f, slot - 2.0f);
        const float seg = juce::jmax (3.0f, std::floor ((bottom - barTop) / 14.0f));
        const int nseg = (int) ((bottom - barTop) / seg);
        for (int i = 0; i < 16; ++i)
        {
            const float bx = gx + (float) i * slot;
            const bool exists = i < data.numParts;
            const float lv = exists ? juce::jlimit (0.0f, 1.0f, data.levels[i]) : 0.0f;
            const int lit = (int) std::ceil (lv * (float) nseg);
            for (int s2 = 0; s2 < nseg; ++s2)
            {
                const float yy = bottom - (float) (s2 + 1) * seg;
                if (s2 < lit) g.setColour (Colours::amber.withAlpha (i + 1 == data.part ? 1.0f : 0.85f));
                else if (s2 == 0) g.setColour (Colours::amber.withAlpha (exists ? 0.5f : 0.15f));
                else continue;
                g.fillRect (bx, yy + 1.0f, barW, seg - 1.0f);
            }
            if (i + 1 == data.part)
            {
                g.setColour (Colours::amber);
                g.fillRect (bx, bottom + 1.0f, barW, 1.5f);
            }
        }
        const auto nf = mono (numSize, false);
        glowText (g, "1", gx, bottom + 2.0f, 20.0f, numSize + 3.0f, nf, Colours::amber.withAlpha (0.7f));
        glowText (g, "8", gx + 7.0f * slot, bottom + 2.0f, 20.0f, numSize + 3.0f, nf, Colours::amber.withAlpha (0.7f));
        glowText (g, "16", x0 + W - 24.0f, bottom + 2.0f, 24.0f, numSize + 3.0f, nf, Colours::amber.withAlpha (0.7f), juce::Justification::centredRight);
    }
};
} // namespace tcui
