#pragma once
#include <JuceHeader.h>

namespace UI
{
    namespace C
    {
        inline const juce::Colour bg      { 0xff0c0d10 };
        inline const juce::Colour panel   { 0xff14161b };
        inline const juce::Colour panel2  { 0xff1a1d23 };
        inline const juce::Colour raised  { 0xff22252d };
        inline const juce::Colour line    { 0xff2a2e37 };
        inline const juce::Colour text    { 0xffece9e2 };
        inline const juce::Colour dim     { 0xff80858f };
        inline const juce::Colour faint   { 0xff4a4f59 };
        inline const juce::Colour accent  { 0xffffb547 };   // amber: the curve, selections
        inline const juce::Colour bass    { 0xff3ad4c4 };   // teal: the bass
        inline const juce::Colour kick    { 0xff9aa0ab };   // grey: the kick
        inline const juce::Colour red     { 0xffff5e5b };
        inline const juce::Colour onDark  { 0xff17130b };
    }

    inline juce::Font font (float h, bool bold = true, float kern = 0.08f)
    {
        return juce::Font (juce::FontOptions (h).withStyle (bold ? "Bold" : "Regular").withKerningFactor (kern));
    }

    inline void label (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c,
                       float h = 11.0f, juce::Justification j = juce::Justification::centredLeft, bool bold = true, float kern = 0.14f)
    {
        g.setColour (c);
        g.setFont (font (h, bold, kern));
        g.drawText (s, r, j, false);
    }

    inline float textWidth (const juce::String& s, float h, bool bold = true, float kern = 0.14f)
    {
        return juce::GlyphArrangement::getStringWidth (font (h, bold, kern), s);
    }

    inline void panelBox (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
    {
        g.setColour (C::panel);
        g.fillRoundedRectangle (r, radius);
        g.setColour (C::line);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    //==========================================================================
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColour (juce::PopupMenu::backgroundColourId, C::panel2);
            setColour (juce::PopupMenu::textColourId, C::text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, C::accent);
            setColour (juce::PopupMenu::highlightedTextColourId, C::onDark);
            setColour (juce::PopupMenu::headerTextColourId, C::dim);
            setColour (juce::AlertWindow::backgroundColourId, C::panel2);
            setColour (juce::AlertWindow::textColourId, C::text);
            setColour (juce::AlertWindow::outlineColourId, C::line);
            setColour (juce::TextEditor::backgroundColourId, C::bg);
            setColour (juce::TextEditor::textColourId, C::text);
            setColour (juce::TextEditor::outlineColourId, C::line);
            setColour (juce::TextEditor::focusedOutlineColourId, C::accent);
            setColour (juce::TextButton::buttonColourId, C::raised);
            setColour (juce::TextButton::textColourOffId, C::text);
            setColour (juce::Slider::textBoxTextColourId, C::text);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxBackgroundColourId, C::bg);
            setColour (juce::Slider::textBoxHighlightColourId, C::accent.withAlpha (0.4f));
        }

        juce::Font getPopupMenuFont() override { return font (13.0f, false, 0.03f); }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                               float a0, float a1, juce::Slider& s) override
        {
            auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6);
            const float d = juce::jmin (r.getWidth(), r.getHeight());
            r = r.withSizeKeepingCentre (d, d);
            const auto ctr = r.getCentre();
            const float rad = d * 0.5f;
            const bool en = s.isEnabled();
            const float ang = a0 + pos * (a1 - a0);
            const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;

            juce::Path track;
            track.addCentredArc (ctr.x, ctr.y, rad - 3, rad - 3, 0, a0, a1, true);
            g.setColour (C::raised);
            g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path val;
            const float from = bipolar ? (a0 + a1) * 0.5f : a0;
            val.addCentredArc (ctr.x, ctr.y, rad - 3, rad - 3, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
            const auto col = en ? C::accent : C::faint;
            g.setColour (col.withAlpha (0.25f));
            g.strokePath (val, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (col);
            g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            auto knob = r.reduced (11);
            juce::ColourGradient grad (C::raised.brighter (0.15f), knob.getX(), knob.getY(), C::bg, knob.getRight(), knob.getBottom(), false);
            g.setGradientFill (grad);
            g.fillEllipse (knob);
            g.setColour (C::line);
            g.drawEllipse (knob, 1.0f);

            const float dr = knob.getWidth() * 0.5f - 7;
            const juce::Point<float> dot (ctr.x + dr * std::sin (ang), ctr.y - dr * std::cos (ang));
            g.setColour (en ? C::text : C::dim);
            g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre (dot));
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (down ? C::raised.brighter (0.1f) : over ? C::raised : C::panel2);
            g.fillRoundedRectangle (r, 6);
            g.setColour (C::line);
            g.drawRoundedRectangle (r, 6, 1);
        }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.0f); }
    };

    //==========================================================================
    // a row of mutually exclusive segments (choice params, tabs)
    class Segmented : public juce::Component
    {
    public:
        explicit Segmented (juce::StringArray items_) : items (std::move (items_)) {}

        std::function<void (int)> onChange;
        float textSize = 12.0f;

        void attach (juce::RangedAudioParameter& p)
        {
            att = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { setSelected ((int) std::round (v), false); });
            onChange = [this] (int i) { att->setValueAsCompleteGesture ((float) i); };
            att->sendInitialUpdate();
        }

        void setSelected (int i, bool notify)
        {
            if (i == selected) return;
            selected = i;
            repaint();
            if (notify && onChange) onChange (i);
        }
        int getSelected() const { return selected; }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (C::panel2);
            g.fillRoundedRectangle (r, 8);
            g.setColour (C::line);
            g.drawRoundedRectangle (r.reduced (0.5f), 8, 1);
            const float w = r.getWidth() / items.size();
            for (int i = 0; i < items.size(); ++i)
            {
                auto seg = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight());
                if (i == selected)
                {
                    g.setColour (isEnabled() ? C::accent : C::faint);
                    g.fillRoundedRectangle (seg.reduced (3), 6);
                }
                label (g, items[i], seg, i == selected ? C::onDark : (hover == i ? C::text : C::dim), textSize, juce::Justification::centred);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! isEnabled()) return;
            const int i = juce::jlimit (0, items.size() - 1, (int) (e.position.x / (getWidth() / (float) items.size())));
            setSelected (i, true);
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const int i = (int) (e.position.x / (getWidth() / (float) items.size()));
            if (i != hover) { hover = i; repaint(); }
        }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    private:
        juce::StringArray items;
        int selected = -1, hover = -1;
        std::unique_ptr<juce::ParameterAttachment> att;
    };

    //==========================================================================
    // pill toggle with an LED dot (bool params)
    class Pill : public juce::Component
    {
    public:
        Pill (juce::String t, bool led = true) : text (std::move (t)), showLed (led) {}
        std::function<void (bool)> onChange;
        std::function<juce::String()> textFn;
        bool solidWhenOn = false;

        void attach (juce::RangedAudioParameter& p)
        {
            att = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { state = v > 0.5f; repaint(); });
            onChange = [this] (bool b) { att->setValueAsCompleteGesture (b ? 1.0f : 0.0f); };
            att->sendInitialUpdate();
        }
        void setState (bool b) { state = b; repaint(); }
        bool getState() const { return state; }
        void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = state;
            g.setColour (on && solidWhenOn ? C::accent : (over ? C::raised : C::panel2));
            g.fillRoundedRectangle (r, 8);
            g.setColour (on ? C::accent.withAlpha (solidWhenOn ? 1.0f : 0.7f) : C::line);
            g.drawRoundedRectangle (r, 8, 1);
            const auto t = textFn ? textFn() : text;
            const auto tc = on && solidWhenOn ? C::onDark : (on ? C::text : C::dim);
            if (showLed)
            {
                const float tw = textWidth (t, 12.0f);
                const float total = tw + 16;
                const float x0 = r.getCentreX() - total * 0.5f;
                auto led = juce::Rectangle<float> (8, 8).withCentre ({ x0 + 4, r.getCentreY() });
                if (on) { g.setColour (C::accent.withAlpha (0.35f)); g.fillEllipse (led.expanded (3)); }
                g.setColour (on ? C::accent : C::faint);
                g.fillEllipse (led);
                label (g, t, { x0 + 16, r.getY(), tw + 4, r.getHeight() }, tc, 12.0f, juce::Justification::centredLeft);
            }
            else label (g, t, r, tc, 12.0f, juce::Justification::centred);
        }
        void mouseDown (const juce::MouseEvent&) override
        {
            if (! isEnabled()) return;
            state = ! state; repaint();
            if (onChange) onChange (state);
        }
        void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }

    private:
        juce::String text;
        bool showLed, state = false, over = false;
        std::unique_ptr<juce::ParameterAttachment> att;
    };

    //==========================================================================
    class Knob : public juce::Component
    {
    public:
        Knob (juce::String name_) : name (std::move (name_))
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            slider.setVelocityBasedMode (false);
            slider.setMouseDragSensitivity (220);
            addAndMakeVisible (slider);
            slider.onValueChange = [this] { repaint(); };
        }
        void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id)
        {
            param = s.getParameter (id);
            att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
        }
        std::function<juce::String()> valueText, nameText;

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromBottom (38);
            slider.setBounds (r);
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            auto b = r.removeFromBottom (38);
            label (g, nameText ? nameText() : name, b.removeFromTop (16), C::dim, 11.0f, juce::Justification::centred);
            const auto v = valueText ? valueText() : (param ? param->getCurrentValueAsText() : juce::String());
            label (g, v, b, isEnabled() ? C::text : C::dim, 15.0f, juce::Justification::centred, true, 0.02f);
        }

        juce::Slider slider;
    private:
        juce::String name;
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    //==========================================================================
    class IconButton : public juce::Component
    {
    public:
        enum Kind { undo, redo };
        explicit IconButton (Kind k) : kind (k) {}
        std::function<void()> onClick;
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (over && isEnabled() ? C::raised : C::panel2);
            g.fillRoundedRectangle (r, 8);
            g.setColour (C::line);
            g.drawRoundedRectangle (r, 8, 1);
            juce::Path p;
            const auto c = r.getCentre();
            p.addCentredArc (c.x, c.y + 2, 6, 6, 0, -juce::MathConstants<float>::halfPi * 1.4f, juce::MathConstants<float>::halfPi * 1.4f, true);
            juce::Path arrow;
            arrow.addTriangle (c.x - 9.5f, c.y - 4.5f, c.x - 4.0f, c.y - 8.5f, c.x - 3.5f, c.y - 0.5f);
            if (kind == redo)
            {
                const auto t = juce::AffineTransform::scale (-1, 1, c.x, c.y);
                p.applyTransform (t); arrow.applyTransform (t);
            }
            const auto col = isEnabled() ? C::text : C::faint;
            g.setColour (col);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillPath (arrow);
        }
        void mouseUp (const juce::MouseEvent& e) override { if (isEnabled() && contains (e.getPosition()) && onClick) onClick(); }
        void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    private:
        Kind kind;
        bool over = false;
    };

    // frequency <-> normalised x on a log axis
    inline float freqToX (float f, float lo = 20.0f, float hi = 20000.0f) { return std::log (f / lo) / std::log (hi / lo); }
    inline float xToFreq (float x, float lo = 20.0f, float hi = 20000.0f) { return lo * std::pow (hi / lo, x); }
    inline juce::String hzText (float f)
    {
        return f >= 1000 ? juce::String (f / 1000.0f, f >= 10000 ? 0 : 1) + "k" : juce::String (juce::roundToInt (f));
    }
}
