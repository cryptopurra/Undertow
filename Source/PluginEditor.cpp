#include "PluginEditor.h"

using namespace UI;

static bool isAudioFile (const juce::String& f)
{
    return f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
        || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".ogg") || f.endsWithIgnoreCase (".mp3");
}

//==============================================================================
// shared context for all pages
struct Ctx
{
    UndertowProcessor& proc;
    juce::AudioProcessorValueTreeState& s;
    DisplayData& d;
    bool dragOver = false;
    std::unique_ptr<juce::FileChooser> chooser;

    explicit Ctx (UndertowProcessor& p) : proc (p), s (p.apvts), d (p.display) {}
    float v (const juce::String& id) const { return s.getRawParameterValue (id)->load(); }
    juce::RangedAudioParameter& p (const juce::String& id) { return *s.getParameter (id); }

    void setParam (const juce::String& id, float value)
    {
        auto& rp = p (id);
        rp.setValueNotifyingHost (rp.convertTo0to1 (value));
    }
    juce::String rateName() const
    {
        static const char* names[] = { "1/16", "1/8", "1/4", "1/2", "1 BAR" };
        return names[juce::jlimit (0, 4, (int) v (ID::rate))];
    }

    void chooseKick (std::function<void()> done)
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a kick sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, done] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile()) proc.loadKickFile (fc.getResult());
                                  if (done) done();
                              });
    }
};

static void noKickOverlay (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (C::panel.withAlpha (0.55f));
    g.fillRect (r);
    label (g, "ROUTE A KICK TO THE SIDECHAIN OR DROP A SAMPLE", r, C::text.withAlpha (0.8f), 13.0f, juce::Justification::centred, true, 0.2f);
}

//==============================================================================
class CurveView : public juce::Component
{
public:
    explicit CurveView (Ctx& c) : ctx (c)
    {
        curve = ctx.proc.getCurve();
        addAndMakeVisible (quant);
        addAndMakeVisible (grid);
        addAndMakeVisible (lanes);
        quant.textSize = 11; lanes.textSize = 11;
        grid.solidWhenOn = true;

        auto& ui = ctx.proc.uiState;
        quant.setSelected ((bool) ui["quantize"] ? 1 : 0, false);
        grid.setState ((bool) ui["grid"]);
        lanes.setSelected ((bool) ui["overlay"] ? 1 : 0, false);
        quant.onChange = [this] (int i) { ctx.proc.uiState.setProperty ("quantize", i == 1, nullptr); };
        grid.onChange  = [this] (bool b) { ctx.proc.uiState.setProperty ("grid", b, nullptr); repaint(); };
        lanes.onChange = [this] (int i) { ctx.proc.uiState.setProperty ("overlay", i == 1, nullptr); repaint(); };
    }

    void sync()
    {
        if (drag.t != Hit::none) return;
        auto c = ctx.proc.getCurve();
        if (c.toString() != curve.toString()) { curve = c; repaint(); }
        auto& ui = ctx.proc.uiState;
        quant.setSelected ((bool) ui["quantize"] ? 1 : 0, false);
        grid.setState ((bool) ui["grid"]);
        lanes.setSelected ((bool) ui["overlay"] ? 1 : 0, false);
    }

    void resized() override
    {
        quant.setBounds (190, 14, 168, 28);
        grid.setBounds (368, 14, 64, 28);
        lanes.setBounds (getWidth() - 182, 14, 166, 28);
    }

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (18, 0).withTrimmedTop (64).withTrimmedBottom (46); }
    float X (float x) const { auto p = plot(); return p.getX() + x * p.getWidth(); }
    float Y (float y) const { auto p = plot(); return p.getBottom() - y * p.getHeight(); }
    float fromX (float px) const { auto p = plot(); return juce::jlimit (0.0f, 1.0f, (px - p.getX()) / p.getWidth()); }
    float fromY (float py) const { auto p = plot(); return juce::jlimit (0.0f, 1.0f, (p.getBottom() - py) / p.getHeight()); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        panelBox (g, b);
        const auto p = plot();
        const float periodMs = ctx.d.periodMs.load();
        const float bpm = ctx.d.bpm.load();
        const float depth = ctx.v (ID::depth) * 0.01f;

        label (g, ctx.rateName() + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + juce::String (bpm, bpm == std::floor (bpm) ? 0 : 1) + " BPM",
               { 20, 12, 170, 18 }, C::accent, 13.0f);
        const bool overlay = lanes.getSelected() == 1;
        label (g, "BEAT", { 20, 34, 40, 14 }, C::kick, 10.0f);
        label (g, "BASS", { 58, 34, 40, 14 }, C::bass, 10.0f);

        // info: level under the kick, opening time
        {
            const float g0 = 1.0f - depth * (1.0f - curve.pts.front().y);
            const juce::String under = g0 <= 1.0e-4f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e dB"))
                                                     : juce::String (juce::Decibels::gainToDecibels (g0), 1) + " dB";
            const float openMs = curve.openAt() * periodMs;
            juce::String txt = "UNDER THE KICK  " + under + "   " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "   OPEN AFTER  "
                             + (curve.openAt() >= 1.0f ? juce::String ("NEVER") : juce::String (juce::roundToInt (openMs)) + " ms");
            label (g, txt, { 440, 14, (float) getWidth() - 640, 28 }, C::dim, 11.0f, juce::Justification::centredRight);
        }

        // grid
        const bool showGrid = grid.getState();
        if (showGrid)
        {
            for (int k = 0; k <= 16; ++k)
            {
                const float x = X (k / 16.0f);
                g.setColour (k % 4 == 0 ? C::line.brighter (0.25f) : C::line.withAlpha (0.6f));
                g.drawVerticalLine (juce::roundToInt (x), p.getY(), p.getBottom());
            }
            for (int k = 0; k <= 4; ++k)
            {
                g.setColour (k == 2 ? C::line.brighter (0.15f) : C::line.withAlpha (0.5f));
                g.drawHorizontalLine (juce::roundToInt (Y (k / 4.0f)), p.getX(), p.getRight());
            }
        }
        for (int q = 1; q < 4; ++q)
            label (g, juce::String (juce::roundToInt (periodMs * q / 4.0f)) + " ms", { X (q / 4.0f) - 40, p.getBottom() + 4, 80, 14 },
                   C::dim, 10.0f, juce::Justification::centred, false, 0.05f);

        drawLanes (g, p, overlay);

        // curve
        juce::Path path, fill;
        buildPath (path);
        fill = path;
        fill.lineTo (X (1), Y (0)); fill.lineTo (X (0), Y (0)); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (C::accent.withAlpha (0.13f), 0, p.getY(), C::accent.withAlpha (0.0f), 0, p.getBottom(), false));
        g.fillPath (fill);
        g.setColour (C::accent.withAlpha (0.16f));
        g.strokePath (path, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (C::accent);
        g.strokePath (path, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // playhead
        const float ph = ctx.d.playhead.load();
        if (ph >= 0 && ph < 1)
        {
            g.setColour (C::text.withAlpha (0.25f));
            g.drawVerticalLine (juce::roundToInt (X (ph)), p.getY(), p.getBottom());
        }

        // bend handles
        for (size_t i = 0; i + 1 < curve.pts.size(); ++i)
            if (auto m = bendHandle ((int) i))
            {
                const bool hot = (hover.t == Hit::bend && hover.i == (int) i) || (drag.t == Hit::bend && drag.i == (int) i);
                auto r = juce::Rectangle<float> (hot ? 11.0f : 9.0f, hot ? 11.0f : 9.0f).withCentre (*m);
                g.setColour (C::panel);
                g.fillEllipse (r);
                g.setColour (C::accent.withAlpha (hot ? 1.0f : 0.8f));
                g.drawEllipse (r, 1.6f);
            }
        // points
        for (size_t i = 0; i < curve.pts.size(); ++i)
        {
            const juce::Point<float> c (X (curve.pts[i].x), Y (curve.pts[i].y));
            const bool hot = (hover.t == Hit::point && hover.i == (int) i) || (drag.t == Hit::point && drag.i == (int) i);
            if (i == 0)
            {
                juce::Path d;
                d.addPolygon (c, 4, hot ? 8.0f : 6.5f, 0.0f);
                g.setColour (C::accent);
                g.fillPath (d);
                continue;
            }
            if (hot) { g.setColour (C::accent.withAlpha (0.3f)); g.fillEllipse (juce::Rectangle<float> (18, 18).withCentre (c)); }
            g.setColour (C::text);
            g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (c));
            g.setColour (C::onDark);
            g.drawEllipse (juce::Rectangle<float> (10, 10).withCentre (c), 1.5f);
        }

        label (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8b BENDS  \xc2\xb7  LINE MOVES  \xc2\xb7  DOUBLE-CLICK ADDS / REMOVES")),
               { p.getRight() - 420, p.getBottom() + 22, 420, 14 }, C::faint, 10.0f, juce::Justification::centredRight);
    }

    // ---------------- mouse ----------------
    struct Hit { enum T { none, point, bend, line, vline } t = none; int i = -1; };

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = hitTestCurve (e.position);
        if (h.t != hover.t || h.i != hover.i) { hover = h; repaint(); }
        setMouseCursor (h.t == Hit::point ? juce::MouseCursor::DraggingHandCursor
                      : h.t == Hit::line || h.t == Hit::bend ? juce::MouseCursor::UpDownResizeCursor
                      : h.t == Hit::vline ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hover = {}; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = hitTestCurve (e.position);
        start = curve;
        startPos = e.position;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag.t == Hit::none) return;
        const bool q = quant.getSelected() == 1;
        auto snapX = [q] (float x) { return q ? std::round (x * 16.0f) / 16.0f : x; };
        auto snapY = [q] (float y) { return q ? std::round (y * 8.0f) / 8.0f : y; };
        auto& pts = curve.pts;
        const int n = (int) pts.size();
        const int i = drag.i;

        if (drag.t == Hit::point)
        {
            float x = snapX (fromX (e.position.x)), y = snapY (fromY (e.position.y));
            if (i == 0) x = 0;
            else if (i == n - 1) x = 1;
            else x = juce::jlimit (pts[(size_t) i - 1].x, pts[(size_t) i + 1].x, x);
            pts[(size_t) i] = { x, y };
        }
        else if (drag.t == Hit::bend)
        {
            const auto& a = pts[(size_t) i];
            const auto& b = pts[(size_t) i + 1];
            if (std::abs (b.y - a.y) > 1.0e-4f)
                curve.bend[(size_t) i] = Curve::bendForMidValue ((fromY (e.position.y) - a.y) / (b.y - a.y));
        }
        else if (drag.t == Hit::line)
        {
            const float dy = fromY (e.position.y) - fromY (startPos.y);
            pts[(size_t) i].y     = snapY (juce::jlimit (0.0f, 1.0f, start.pts[(size_t) i].y + dy));
            pts[(size_t) i + 1].y = snapY (juce::jlimit (0.0f, 1.0f, start.pts[(size_t) i + 1].y + dy));
        }
        else if (drag.t == Hit::vline)
        {
            if (i == 0 || i + 1 >= n - 1) return;
            const float w = start.pts[(size_t) i + 1].x - start.pts[(size_t) i].x;
            float x = snapX (start.pts[(size_t) i].x + fromX (e.position.x) - fromX (startPos.x));
            x = juce::jlimit (pts[(size_t) i - 1].x, pts[(size_t) i + 2].x - w, x);
            pts[(size_t) i].x = x;
            pts[(size_t) i + 1].x = x + w;
        }
        ctx.proc.setCurve (curve);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        drag = {};
        ctx.proc.setCurve (curve);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto h = hitTestCurve (e.position);
        auto& pts = curve.pts;
        if (h.t == Hit::point)
        {
            if (h.i > 0 && h.i < (int) pts.size() - 1)
            {
                pts.erase (pts.begin() + h.i);
                curve.bend.erase (curve.bend.begin() + h.i);
            }
        }
        else if (plot().expanded (6).contains (e.position))
        {
            const float x = fromX (e.position.x), y = fromY (e.position.y);
            size_t seg = 0;
            for (size_t k = 0; k + 1 < pts.size(); ++k)
                if (pts[k].x <= x && x <= pts[k + 1].x) seg = k;
            if (x <= 0.0f || x >= 1.0f) return;
            pts.insert (pts.begin() + (long) seg + 1, { x, y });
            curve.bend.insert (curve.bend.begin() + (long) seg + 1, curve.bend[seg]);
        }
        else return;
        drag = {};
        ctx.proc.setCurve (curve);
        repaint();
    }

private:
    std::optional<juce::Point<float>> bendHandle (int i) const
    {
        const auto& a = curve.pts[(size_t) i];
        const auto& b = curve.pts[(size_t) i + 1];
        if (b.x - a.x < 0.03f || std::abs (b.y - a.y) < 0.04f) return std::nullopt;
        const float v = Curve::shape (0.5f, curve.bend[(size_t) i]);
        return juce::Point<float> (X (a.x + 0.5f * (b.x - a.x)), Y (a.y + v * (b.y - a.y)));
    }

    Hit hitTestCurve (juce::Point<float> pos) const
    {
        Hit best;
        float bestD = 10.0f;
        for (size_t i = 0; i < curve.pts.size(); ++i)
        {
            const float d = pos.getDistanceFrom ({ X (curve.pts[i].x), Y (curve.pts[i].y) });
            if (d < bestD) { bestD = d; best = { Hit::point, (int) i }; }
        }
        if (best.t != Hit::none) return best;
        for (size_t i = 0; i + 1 < curve.pts.size(); ++i)
            if (auto m = bendHandle ((int) i); m && pos.getDistanceFrom (*m) < 9.0f) return { Hit::bend, (int) i };
        for (size_t i = 0; i + 1 < curve.pts.size(); ++i)
        {
            const auto& a = curve.pts[i];
            const auto& b = curve.pts[i + 1];
            const float xa = X (a.x), xb = X (b.x);
            if (xb - xa < 4.0f)
            {
                const float y0 = juce::jmin (Y (a.y), Y (b.y)), y1 = juce::jmax (Y (a.y), Y (b.y));
                if (std::abs (pos.x - xa) < 6.0f && pos.y > y0 && pos.y < y1) return { Hit::vline, (int) i };
            }
            else if (pos.x >= xa && pos.x <= xb)
            {
                const float u = (pos.x - xa) / (xb - xa);
                const float cy = Y (a.y + (b.y - a.y) * Curve::shape (u, curve.bend[i]));
                if (std::abs (pos.y - cy) < 7.0f) return { Hit::line, (int) i };
            }
        }
        return {};
    }

    void buildPath (juce::Path& path) const
    {
        const auto& pts = curve.pts;
        path.startNewSubPath (X (pts[0].x), Y (pts[0].y));
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            const auto& a = pts[i];
            const auto& b = pts[i + 1];
            if (b.x - a.x < 1.0e-4f || std::abs (b.y - a.y) < 1.0e-4f || std::abs (curve.bend[i]) < 1.0e-4f)
                path.lineTo (X (b.x), Y (b.y));
            else
                for (int k = 1; k <= 48; ++k)
                {
                    const float u = k / 48.0f;
                    path.lineTo (X (a.x + u * (b.x - a.x)), Y (a.y + (b.y - a.y) * Curve::shape (u, curve.bend[i])));
                }
        }
    }

    void drawLanes (juce::Graphics& g, juce::Rectangle<float> p, bool overlay)
    {
        constexpr int N = DisplayData::cycleBins;
        float k[N], bIn[N], bOut[N];
        float mk = 1.0e-6f, mb = 1.0e-6f;
        for (int i = 0; i < N; ++i)
        {
            k[i] = ctx.d.kick[(size_t) i].load (std::memory_order_relaxed);
            bIn[i] = ctx.d.bass[(size_t) i].load (std::memory_order_relaxed);
            bOut[i] = ctx.d.out[(size_t) i].load (std::memory_order_relaxed);
            mk = juce::jmax (mk, k[i]); mb = juce::jmax (mb, bIn[i]);
        }
        auto env = [&] (const float* v, float norm, float centre, float half, juce::Colour col, float alpha)
        {
            juce::Path e;
            e.startNewSubPath (p.getX(), centre);
            for (int i = 0; i < N; ++i) e.lineTo (p.getX() + (i + 0.5f) / N * p.getWidth(), centre - half * std::sqrt (juce::jmin (1.0f, v[i] / norm)));
            e.lineTo (p.getRight(), centre);
            for (int i = N; --i >= 0;) e.lineTo (p.getX() + (i + 0.5f) / N * p.getWidth(), centre + half * std::sqrt (juce::jmin (1.0f, v[i] / norm)));
            e.closeSubPath();
            g.setColour (col.withAlpha (alpha));
            g.fillPath (e);
        };
        if (mk < 1.0e-4f && mb < 1.0e-4f) return;
        if (overlay)
        {
            const float c = p.getCentreY(), h = p.getHeight() * 0.42f;
            env (k, mk, c, h, C::kick, 0.16f);
            env (bIn, mb, c, h, C::bass, 0.07f);
            env (bOut, mb, c, h, C::bass, 0.22f);
        }
        else
        {
            const float h = p.getHeight() * 0.22f;
            env (k, mk, p.getY() + p.getHeight() * 0.25f, h, C::kick, 0.2f);
            env (bIn, mb, p.getY() + p.getHeight() * 0.75f, h, C::bass, 0.08f);
            env (bOut, mb, p.getY() + p.getHeight() * 0.75f, h, C::bass, 0.26f);
        }
    }

    Ctx& ctx;
    Curve curve, start;
    juce::Point<float> startPos;
    Hit hover, drag;
    Segmented quant { { "FREE", "QUANTIZE" } };
    Pill grid { "GRID", false };
    Segmented lanes { { "LANES", "OVERLAY" } };
};

//==============================================================================
class ShapeStrip : public juce::Component
{
public:
    explicit ShapeStrip (Ctx& c) : ctx (c) { load(); }

    void load()
    {
        shapes = Shapes::builtIn();
        auto file = UndertowProcessor::getUserDir().getChildFile ("shapes.xml");
        juce::StringArray order;
        if (auto xml = juce::parseXML (file))
        {
            for (auto* e : xml->getChildWithTagNameIterator ("SHAPE"))
                shapes.push_back ({ e->getStringAttribute ("id"), e->getStringAttribute ("name"),
                                    Curve::fromString (e->getStringAttribute ("pts")), true });
            order.addTokens (xml->getStringAttribute ("order"), ",", "");
        }
        if (! order.isEmpty())
        {
            std::vector<Shapes::Shape> sorted;
            for (auto& id : order)
                for (auto& s : shapes) if (s.id == id) { sorted.push_back (s); break; }
            for (auto& s : shapes)
                if (! order.contains (s.id)) sorted.push_back (s);
            shapes = sorted;
        }
        repaint();
    }

    void save()
    {
        juce::XmlElement xml ("SHAPES");
        juce::StringArray order;
        for (auto& s : shapes)
        {
            order.add (s.id);
            if (! s.user) continue;
            auto* e = xml.createNewChildElement ("SHAPE");
            e->setAttribute ("id", s.id);
            e->setAttribute ("name", s.name);
            e->setAttribute ("pts", s.curve.toString());
        }
        xml.setAttribute ("order", order.joinIntoString (","));
        xml.writeTo (UndertowProcessor::getUserDir().getChildFile ("shapes.xml"));
    }

    void addUserShape (const juce::String& name)
    {
        shapes.push_back ({ "user-" + juce::String (juce::Time::currentTimeMillis()), name, ctx.proc.getCurve(), true });
        save();
        repaint();
    }

    juce::String hoverName() const { return juce::isPositiveAndBelow (hover, (int) shapes.size()) ? shapes[(size_t) hover].name : juce::String(); }

    void paint (juce::Graphics& g) override
    {
        const auto cur = ctx.proc.getCurve().toString();
        for (int i = 0; i < (int) shapes.size(); ++i)
        {
            auto r = tile (i);
            if (dragging && i == dragFrom) continue;
            drawTile (g, r, shapes[(size_t) i], shapes[(size_t) i].curve.toString() == cur, i == hover);
        }
        if (dragging)
        {
            const float x = juce::jlimit (0.0f, (float) getWidth() - tileW(), dragX - tileW() * 0.5f);
            const int t = targetIndex();
            g.setColour (C::accent);
            g.fillRect (tile (t).getX() - 4.0f, 2.0f, 2.0f, (float) getHeight() - 4);
            drawTile (g, { x, 0, tileW(), (float) getHeight() }, shapes[(size_t) dragFrom], false, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = indexAt (e.position.x);
        if (h != hover) { hover = h; repaint(); if (getParentComponent()) getParentComponent()->repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); if (getParentComponent()) getParentComponent()->repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragFrom = indexAt (e.position.x);
        dragging = false;
        if (e.mods.isPopupMenu() && juce::isPositiveAndBelow (dragFrom, (int) shapes.size()) && shapes[(size_t) dragFrom].user)
        {
            juce::PopupMenu m;
            m.addItem (1, "Delete \"" + shapes[(size_t) dragFrom].name + "\"");
            const int idx = dragFrom;
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, idx] (int r)
            {
                if (r == 1 && juce::isPositiveAndBelow (idx, (int) shapes.size())) { shapes.erase (shapes.begin() + idx); save(); repaint(); }
            });
            dragFrom = -1;
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFrom < 0) return;
        if (! dragging && e.getDistanceFromDragStart() > 6) dragging = true;
        dragX = e.position.x;
        if (dragging) repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragFrom < 0) return;
        if (dragging)
        {
            int t = targetIndex();
            auto s = shapes[(size_t) dragFrom];
            shapes.erase (shapes.begin() + dragFrom);
            if (t > dragFrom) --t;
            shapes.insert (shapes.begin() + juce::jlimit (0, (int) shapes.size(), t), s);
            save();
        }
        else if (indexAt (e.position.x) == dragFrom && juce::isPositiveAndBelow (dragFrom, (int) shapes.size()))
        {
            ctx.proc.setCurve (shapes[(size_t) dragFrom].curve);
        }
        dragging = false; dragFrom = -1;
        repaint();
    }

private:
    float gap() const { return 6.0f; }
    float tileW() const
    {
        const int n = juce::jmax (1, (int) shapes.size());
        return juce::jlimit (34.0f, 66.0f, ((float) getWidth() - gap() * (n - 1)) / n);
    }
    juce::Rectangle<float> tile (int i) const { return { i * (tileW() + gap()), 0, tileW(), (float) getHeight() }; }
    int indexAt (float x) const
    {
        const int i = (int) (x / (tileW() + gap()));
        return juce::isPositiveAndBelow (i, (int) shapes.size()) && x - i * (tileW() + gap()) <= tileW() ? i : -1;
    }
    int targetIndex() const { return juce::jlimit (0, (int) shapes.size(), (int) std::round (dragX / (tileW() + gap()))); }

    static void drawTile (juce::Graphics& g, juce::Rectangle<float> r, const Shapes::Shape& s, bool selected, bool hot)
    {
        g.setColour (selected ? C::accent.withAlpha (0.12f) : hot ? C::raised : C::panel2);
        g.fillRoundedRectangle (r.reduced (0.5f), 7);
        g.setColour (selected ? C::accent : C::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 7, selected ? 1.4f : 1.0f);
        auto in = r.reduced (9, 8);
        juce::Path p;
        for (int k = 0; k <= 60; ++k)
        {
            const float x = k / 60.0f;
            const juce::Point<float> pt (in.getX() + x * in.getWidth(), in.getBottom() - s.curve.eval (x) * in.getHeight());
            if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (selected ? C::accent : (s.user ? C::bass : C::text.withAlpha (0.75f)));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    Ctx& ctx;
    std::vector<Shapes::Shape> shapes;
    int hover = -1, dragFrom = -1;
    bool dragging = false;
    float dragX = 0;
};

//==============================================================================
// which part of the bass the duck works on
class BandStrip : public juce::Component
{
public:
    explicit BandStrip (Ctx& c) : ctx (c) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (C::bg);
        g.fillRoundedRectangle (r, 6);
        g.setColour (C::line);
        g.drawRoundedRectangle (r, 6, 1);
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.setColour (C::line.withAlpha (0.7f));
            g.drawVerticalLine (juce::roundToInt (r.getX() + freqToX (f) * r.getWidth()), r.getY() + 3, r.getBottom() - 3);
        }
        const int duck = (int) ctx.v (ID::duck);
        float lo = 20, hi = 20000;
        juce::String txt = "THE WHOLE BASS";
        if (duck == 1) { hi = 120; txt = "SUB ONLY  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  BELOW 120 Hz"; }
        if (duck == 2)
        {
            lo = ctx.v (ID::bandLo); hi = ctx.v (ID::bandHi);
            txt = hzText (lo) + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93 ")) + hzText (hi) + " Hz";
        }
        auto sel = juce::Rectangle<float> (r.getX() + freqToX (lo) * r.getWidth(), r.getY(),
                                           (freqToX (hi) - freqToX (lo)) * r.getWidth(), r.getHeight());
        g.setColour (C::accent.withAlpha (0.18f));
        g.fillRoundedRectangle (sel, 5);
        if (duck == 2)
            for (float x : { sel.getX(), sel.getRight() })
            {
                g.setColour (C::accent);
                g.fillRoundedRectangle (juce::Rectangle<float> (4, r.getHeight() - 6).withCentre ({ x, r.getCentreY() }), 2);
            }
        label (g, txt, r, C::text.withAlpha (0.85f), 11.0f, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        handle = -1;
        if ((int) ctx.v (ID::duck) != 2) return;
        const float f = xToFreq (juce::jlimit (0.0f, 1.0f, e.position.x / getWidth()));
        const float lo = ctx.v (ID::bandLo), hi = ctx.v (ID::bandHi);
        handle = std::abs (std::log (f / lo)) < std::abs (std::log (f / hi)) ? 0 : 1;
        param().beginChangeGesture();
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (handle < 0) return;
        float f = xToFreq (juce::jlimit (0.0f, 1.0f, e.position.x / getWidth()));
        if (handle == 0) f = juce::jmin (f, ctx.v (ID::bandHi) / 1.25f);
        else f = juce::jmax (f, ctx.v (ID::bandLo) * 1.25f);
        param().setValueNotifyingHost (param().convertTo0to1 (f));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (handle >= 0) param().endChangeGesture(); handle = -1; }

private:
    juce::RangedAudioParameter& param() { return ctx.p (handle == 0 ? ID::bandLo : ID::bandHi); }
    Ctx& ctx;
    int handle = -1;
};

//==============================================================================
class KickDrop : public juce::Component
{
public:
    explicit KickDrop (Ctx& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1);
        const auto name = ctx.proc.getKickName();
        const bool hot = ctx.dragOver;
        juce::Path border;
        border.addRoundedRectangle (r, 7);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, border, dashes, 2);
        if (hot) { g.setColour (C::accent.withAlpha (0.15f)); g.fillRoundedRectangle (r, 7); }
        g.setColour (hot ? C::accent : C::faint);
        g.fillPath (dashed);
        if (name.isNotEmpty())
        {
            label (g, name.toUpperCase(), r.withTrimmedLeft (10).withTrimmedRight (24), C::bass, 11.0f, juce::Justification::centredLeft, true, 0.06f);
            label (g, juce::String (juce::CharPointer_UTF8 ("\xc3\x97")), r.removeFromRight (24), C::dim, 15.0f, juce::Justification::centred);
        }
        else
            label (g, juce::String (juce::CharPointer_UTF8 ("\xe2\xa4\x93  DROP A KICK HERE")), r, hot ? C::accent : C::dim, 11.0f, juce::Justification::centred);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (ctx.proc.getKickName().isNotEmpty() && e.position.x > getWidth() - 26) ctx.proc.clearKick();
        else ctx.chooseKick ([this] { repaint(); });
        repaint();
    }
private:
    Ctx& ctx;
};

//==============================================================================
class SidechainPage : public juce::Component
{
public:
    explicit SidechainPage (Ctx& c) : ctx (c), curveView (c), shapes (c), band (c), drop (c)
    {
        addAndMakeVisible (curveView);
        addAndMakeVisible (shapes);
        addAndMakeVisible (saveBtn);
        addAndMakeVisible (depth); depth.attach (ctx.s, ID::depth);
        addAndMakeVisible (click); click.attach (ctx.s, ID::click);
        addAndMakeVisible (duck);  duck.attach (ctx.p (ID::duck));
        addAndMakeVisible (band);
        addAndMakeVisible (loadBtn);
        addAndMakeVisible (drop);
        click.valueText = [this] { return juce::String (ctx.v (ID::click), 1) + " ms"; };
        loadBtn.onClick = [this] { ctx.chooseKick ([this] { drop.repaint(); }); };
        saveBtn.onClick = [this]
        {
            auto* w = new juce::AlertWindow ("Save shape", "Name for this curve:", juce::MessageBoxIconType::NoIcon, this);
            w->setLookAndFeel (&getLookAndFeel());
            w->addTextEditor ("name", "My shape");
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<SidechainPage> safe (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
            {
                if (r == 1 && safe != nullptr)
                {
                    auto name = w->getTextEditorContents ("name").trim();
                    safe->shapes.addUserShape (name.isEmpty() ? "My shape" : name);
                }
            }), true);
        };
    }

    void resized() override
    {
        curveView.setBounds (0, 0, getWidth(), 360);
        shapes.setBounds (96, 372, getWidth() - 96 - 110, 40);
        saveBtn.setBounds (getWidth() - 96, 376, 96, 32);
        const int y = 424;
        depth.setBounds (14, y + 10, 104, 138);
        click.setBounds (122, y + 10, 104, 138);
        duck.setBounds (262, y + 44, 204, 34);
        band.setBounds (262, y + 92, 320, 32);
        loadBtn.setBounds (616, y + 44, 136, 34);
        drop.setBounds (762, y + 44, 168, 34);
    }

    void paint (juce::Graphics& g) override
    {
        auto hv = shapes.hoverName();
        label (g, hv.isEmpty() ? juce::String ("SHAPES") : hv.toUpperCase(), { 4, 372, 92, 40 }, hv.isEmpty() ? C::dim : C::accent, 11.0f);
        auto r = juce::Rectangle<float> (0, 424, (float) getWidth(), (float) getHeight() - 424);
        panelBox (g, r);
        label (g, "DUCK", { 262, r.getY() + 16, 200, 18 }, C::dim, 11.0f);
        label (g, "KICK", { 616, r.getY() + 16, 200, 18 }, C::dim, 11.0f);
        label (g, "WITHOUT ROUTING THE SAMPLE FOLLOWS THE GRID", { 616, r.getY() + 92, 330, 32 }, C::faint, 10.0f);

        const float x = 966;
        g.setColour (C::line);
        g.drawVerticalLine ((int) x - 18, r.getY() + 18, r.getBottom() - 18);
        const juce::String lines[] = { juce::String (juce::CharPointer_UTF8 ("KICK IN \xe2\x86\x92 CYCLE RESTARTS")), "ZERO BASS ON THE HIT",
                                       "DUCK PICKS THE RANGE", "OUTSIDE IT: UNTOUCHED" };
        for (int i = 0; i < 4; ++i)
            label (g, lines[i], { x, r.getY() + 22 + i * 26.0f, (float) getWidth() - x - 10, 20 },
                   i == 0 ? C::text : C::dim, 10.5f, juce::Justification::centredLeft, true, 0.08f);
    }

    void sync() { curveView.sync(); band.repaint(); drop.repaint(); }

    Ctx& ctx;
    CurveView curveView;
    ShapeStrip shapes;
    juce::TextButton saveBtn { "+ SAVE" };
    Knob depth { "DEPTH" }, click { "CLICK" };
    Segmented duck { { "FULL", "SUB", "BAND" } };
    BandStrip band;
    juce::TextButton loadBtn { juce::String (juce::CharPointer_UTF8 ("LOAD KICK\xe2\x80\xa6")) };
    KickDrop drop;
};

//==============================================================================
class PhaseScope : public juce::Component
{
public:
    explicit PhaseScope (Ctx& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        panelBox (g, b);
        label (g, juce::String (juce::CharPointer_UTF8 ("KICK TAIL \xe2\x86\x92 BASS")), { 20, 14, 220, 18 }, C::text, 12.0f);
        label (g, "LOW END OF THE KICK  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  SUB OF THE BASS AS YOU HEAR IT",
               { 200, 14, 600, 18 }, C::dim, 10.5f);
        label (g, "KICK " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " LOW BAND", { b.getRight() - 260, 12, 240, 14 }, C::kick, 10.0f, juce::Justification::centredRight);
        label (g, "BASS " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " SUB AS HEARD", { b.getRight() - 260, 28, 240, 14 }, C::bass, 10.0f, juce::Justification::centredRight);

        auto p = b.reduced (18, 0).withTrimmedTop (52).withTrimmedBottom (30);
        const float win = ctx.d.phaseWindowMs.load();
        for (int k = 0; k <= 15; ++k)
        {
            const float x = p.getX() + k / 15.0f * p.getWidth();
            g.setColour (k % 4 == 0 ? C::line.brighter (0.2f) : C::line.withAlpha (0.5f));
            g.drawVerticalLine (juce::roundToInt (x), p.getY(), p.getBottom());
        }
        for (int ms : { 40, 80, 120 })
            label (g, juce::String (ms) + " ms", { p.getX() + ms / win * p.getWidth() - 30, p.getBottom() + 8, 60, 14 }, C::dim, 10.0f, juce::Justification::centred, false, 0.05f);
        g.setColour (C::line.brighter (0.2f));
        g.drawHorizontalLine (juce::roundToInt (p.getCentreY()), p.getX(), p.getRight());

        constexpr int N = DisplayData::phaseBins;
        float k[N], s[N], mx = 1.0e-6f;
        for (int i = 0; i < N; ++i)
        {
            k[i] = ctx.d.phKick[(size_t) i].load (std::memory_order_relaxed);
            s[i] = ctx.d.phBass[(size_t) i].load (std::memory_order_relaxed);
            mx = juce::jmax (mx, std::abs (k[i]), std::abs (s[i]));
        }
        auto wave = [&] (const float* v, juce::Colour col, float w)
        {
            juce::Path path;
            for (int i = 0; i < N; ++i)
            {
                const juce::Point<float> pt (p.getX() + i / (float) (N - 1) * p.getWidth(), p.getCentreY() - v[i] / mx * p.getHeight() * 0.44f);
                if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
            }
            g.setColour (col.withAlpha (0.18f));
            g.strokePath (path, juce::PathStrokeType (w * 3.5f, juce::PathStrokeType::curved));
            g.setColour (col);
            g.strokePath (path, juce::PathStrokeType (w, juce::PathStrokeType::curved));
        };
        if (mx > 1.0e-4f)
        {
            wave (k, C::kick, 1.8f);
            wave (s, C::bass, 2.2f);
        }
        if (! ctx.d.kickRef.load()) noKickOverlay (g, p);
    }
private:
    Ctx& ctx;
};

class PhasePage : public juce::Component
{
public:
    explicit PhasePage (Ctx& c) : ctx (c), scope (c)
    {
        addAndMakeVisible (scope);
        addAndMakeVisible (on);    on.attach (ctx.p (ID::phaseOn));
        addAndMakeVisible (angle); angle.attach (ctx.s, ID::phaseAngle);
        addChildComponent (autoAngle);
        addAndMakeVisible (splitK); splitK.attach (ctx.s, ID::split);
        addAndMakeVisible (mode);  mode.attach (ctx.p (ID::phaseMode));
        addAndMakeVisible (flip);  flip.attach (ctx.p (ID::flip));
        autoAngle.slider.setRange (-180, 180);
        autoAngle.slider.setInterceptsMouseClicks (false, false);
        autoAngle.valueText = [this] { return juce::String (juce::roundToInt (ctx.d.angleDeg.load())) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); };
        splitK.valueText = [this] { return juce::String (juce::roundToInt (ctx.v (ID::split))) + " Hz"; };
    }
    void resized() override
    {
        scope.setBounds (0, 0, getWidth(), 400);
        const int y = 412;
        on.setBounds (22, y + 44, 70, 32);
        angle.setBounds (108, y + 12, 112, 146);
        autoAngle.setBounds (angle.getBounds());
        splitK.setBounds (226, y + 12, 112, 146);
        mode.setBounds (360, y + 44, 180, 34);
        flip.setBounds (360, y + 92, 90, 32);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = juce::Rectangle<float> (0, 412, (float) getWidth(), (float) getHeight() - 412);
        panelBox (g, r);
        label (g, "PHASE", { 22, r.getY() + 16, 80, 18 }, C::dim, 11.0f);
        label (g, "MODE", { 360, r.getY() + 16, 80, 18 }, C::dim, 11.0f);
        const float x = 590;
        g.setColour (C::line);
        g.drawVerticalLine ((int) x - 20, r.getY() + 18, r.getBottom() - 18);
        const bool kick = ctx.d.kickRef.load();
        const bool autoMode = (int) ctx.v (ID::phaseMode) == 0;
        label (g, ! kick ? "NO KICK TO LISTEN TO" : autoMode ? "FOLLOWING THE KICK" : "MANUAL ROTATION",
               { x, r.getY() + 16, 560, 18 }, C::text, 12.0f);
        const juce::String l1 = "AUTO  finds the rotation where the kick's tail and the bass sub add up, and turns only the sub (below SPLIT).";
        const juce::String l2 = "Linear-phase, so the rest of the bass is untouched. The turn happens only while the duck is closed.";
        const juce::String l3 = juce::String (juce::CharPointer_UTF8 ("MANUAL  the rotation is yours.   \xc3\x98 FLIP  inverts the sub."));
        g.setFont (font (11.5f, false, 0.02f));
        g.setColour (C::dim);
        g.drawFittedText (l1 + "\n" + l2 + "\n" + l3, juce::Rectangle<float> (x, r.getY() + 42, (float) getWidth() - x - 16, 70).toNearestInt(),
                          juce::Justification::topLeft, 4, 1.0f);
        if (kick && autoMode)
            label (g, "MATCH  " + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, ctx.d.coherence.load()) * 100)) + "%   "
                      + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "   TARGET  " + juce::String (juce::roundToInt (ctx.d.targetDeg.load()))
                      + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")),
                   { x, r.getBottom() - 34, 400, 18 }, C::accent, 11.0f);
    }
    void sync()
    {
        const bool autoMode = (int) ctx.v (ID::phaseMode) == 0;
        angle.setVisible (! autoMode);
        autoAngle.setVisible (autoMode);
        if (autoMode) { autoAngle.slider.setValue (ctx.d.angleDeg.load(), juce::dontSendNotification); autoAngle.repaint(); }
        scope.repaint();
        repaint (0, 412, getWidth(), getHeight() - 412);
    }
    Ctx& ctx;
    PhaseScope scope;
    Pill on { "ON" };
    Knob angle { "PHASE " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " MANUAL" };
    Knob autoAngle { "PHASE " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " AUTO" };
    Knob splitK { "SPLIT" };
    Segmented mode { { "AUTO", "MANUAL" } };
    Pill flip { juce::String (juce::CharPointer_UTF8 ("\xc3\x98 FLIP")) };
};

//==============================================================================
class BellsView : public juce::Component
{
public:
    explicit BellsView (Ctx& c) : ctx (c) {}
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (20, 0).withTrimmedTop (46).withTrimmedBottom (28).withTrimmedRight (34); }
    static constexpr float fLo = 20, fHi = 20000, dbRange = 30;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        panelBox (g, b);
        label (g, "THE BELLS", { 20, 14, 100, 18 }, C::text, 12.0f);
        label (g, "LIVE  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  DYNAMIC EQ ON THE BASS  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  KICK IN GREY, BASS IN TEAL",
               { 120, 14, 600, 18 }, C::dim, 10.5f);
        const auto p = plot();
        for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = p.getX() + freqToX (f, fLo, fHi) * p.getWidth();
            g.setColour (C::line.withAlpha (0.6f));
            g.drawVerticalLine (juce::roundToInt (x), p.getY(), p.getBottom());
            label (g, hzText (f), { x - 20, p.getBottom() + 6, 40, 14 }, C::dim, 10.0f, juce::Justification::centred, false, 0.04f);
        }
        for (int db = 0; db >= -24; db -= 6)
        {
            const float y = dbY (db);
            g.setColour (C::line.withAlpha (db == 0 ? 1.0f : 0.5f));
            g.drawHorizontalLine (juce::roundToInt (y), p.getX(), p.getRight());
            label (g, juce::String (db) + " dB", { p.getRight() + 2, y - 7, 40, 14 }, C::faint, 9.5f, juce::Justification::centredLeft, false, 0.02f);
        }

        constexpr int NB = SpaceEQ::numBands;
        float fr[NB], ke[NB], be[NB], gd[NB], q[NB], km = 1.0e-6f, bm = 1.0e-6f;
        for (int i = 0; i < NB; ++i)
        {
            fr[i] = ctx.d.bandFreq[(size_t) i].load(); ke[i] = ctx.d.kEnv[(size_t) i].load(); be[i] = ctx.d.bEnv[(size_t) i].load();
            gd[i] = ctx.d.gainDb[(size_t) i].load(); q[i] = ctx.d.bandQ[(size_t) i].load();
            km = juce::jmax (km, ke[i]); bm = juce::jmax (bm, be[i]);
        }
        const float lo = ctx.v (ID::spaceLo), hi = ctx.v (ID::spaceHi);
        auto fx = [&] (float f) { return p.getX() + freqToX (f, fLo, fHi) * p.getWidth(); };

        // the range
        g.setColour (C::accent.withAlpha (0.05f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (fx (lo), p.getY(), fx (hi), p.getBottom()));

        if (fr[0] > 0)
        {
            auto spectrum = [&] (const float* e, float mx, juce::Colour col, bool filled)
            {
                juce::Path s;
                s.startNewSubPath (fx (fr[0]), p.getBottom());
                for (int i = 0; i < NB; ++i)
                {
                    const float db = juce::jlimit (-dbRange, 0.0f, juce::Decibels::gainToDecibels (e[i] / mx, -100.0f));
                    s.lineTo (fx (fr[i]), juce::jmap (db, -dbRange, 0.0f, p.getBottom(), p.getY() + p.getHeight() * 0.1f));
                }
                s.lineTo (fx (fr[NB - 1]), p.getBottom());
                if (filled) { s.closeSubPath(); g.setColour (col.withAlpha (0.16f)); g.fillPath (s); }
                g.setColour (col.withAlpha (0.8f));
                g.strokePath (s, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
            };
            if (km > 1.0e-4f) spectrum (ke, km, C::kick, true);
            if (bm > 1.0e-4f) spectrum (be, bm, C::bass, false);

            // eq response
            juce::Path eq;
            for (int k = 0; k <= 300; ++k)
            {
                const float f = xToFreq (k / 300.0f, fLo, fHi);
                float db = 0;
                for (int i = 0; i < NB; ++i)
                {
                    const float t = q[i] * (f / fr[i] - fr[i] / f);
                    db += gd[i] / (1.0f + t * t);
                }
                const juce::Point<float> pt (p.getX() + k / 300.0f * p.getWidth(), dbY (juce::jmax (-27.0f, db)));
                if (k == 0) eq.startNewSubPath (pt); else eq.lineTo (pt);
            }
            g.setColour (C::red.withAlpha (0.25f));
            g.strokePath (eq, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved));
            g.setColour (C::red);
            g.strokePath (eq, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
        }

        for (int h = 0; h < 2; ++h)
        {
            const float f = h == 0 ? lo : hi;
            const float x = fx (f);
            g.setColour (C::accent.withAlpha (drag == h ? 1.0f : 0.7f));
            g.drawLine (x, p.getY(), x, p.getBottom(), 1.5f);
            g.fillRect (juce::Rectangle<float> (8, 8).withCentre ({ x, p.getY() + 4 }));
            label (g, (h == 0 ? "LOW " : "HIGH ") + hzText (f) + " Hz", { x + 8, p.getY() + 16, 110, 14 }, C::accent, 10.0f);
        }
        if (! ctx.d.kickRef.load()) noKickOverlay (g, p);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = plot();
        const float lo = ctx.v (ID::spaceLo), hi = ctx.v (ID::spaceHi);
        const float xl = p.getX() + freqToX (lo, fLo, fHi) * p.getWidth(), xh = p.getX() + freqToX (hi, fLo, fHi) * p.getWidth();
        drag = std::abs (e.position.x - xl) < std::abs (e.position.x - xh) ? 0 : 1;
        if (std::abs (e.position.x - (drag == 0 ? xl : xh)) > 30) { drag = -1; return; }
        param().beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const auto p = plot();
        float f = xToFreq (juce::jlimit (0.0f, 1.0f, (e.position.x - p.getX()) / p.getWidth()), fLo, fHi);
        if (drag == 0) f = juce::jmin (f, ctx.v (ID::spaceHi) / 1.5f);
        else f = juce::jmax (f, ctx.v (ID::spaceLo) * 1.5f);
        param().setValueNotifyingHost (param().convertTo0to1 (f));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (drag >= 0) param().endChangeGesture(); drag = -1; repaint(); }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = plot();
        bool near = false;
        for (auto id : { ID::spaceLo, ID::spaceHi })
            near |= std::abs (e.position.x - (p.getX() + freqToX (ctx.v (id), fLo, fHi) * p.getWidth())) < 10;
        setMouseCursor (near ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

private:
    float dbY (float db) const { auto p = plot(); return juce::jmap (db, -27.0f, 0.0f, p.getBottom(), p.getY() + 10); }
    juce::RangedAudioParameter& param() { return ctx.p (drag == 0 ? ID::spaceLo : ID::spaceHi); }
    Ctx& ctx;
    int drag = -1;
};

class SpaceTimeline : public juce::Component
{
public:
    explicit SpaceTimeline (Ctx& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        panelBox (g, b);
        label (g, "WHEN THEY WORK", { 20, 12, 140, 18 }, C::text, 12.0f);
        label (g, "ONE CYCLE  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  RED = BELLS CUTTING", { 160, 12, 400, 18 }, C::dim, 10.5f);
        label (g, "BEAT", { b.getRight() - 70, 10, 52, 12 }, C::kick, 9.5f, juce::Justification::centredRight);
        label (g, "BASS", { b.getRight() - 70, 22, 52, 12 }, C::bass, 9.5f, juce::Justification::centredRight);
        auto p = b.reduced (20, 0).withTrimmedTop (40).withTrimmedBottom (14);
        g.setColour (C::bg);
        g.fillRoundedRectangle (p, 5);
        constexpr int N = DisplayData::cycleBins;
        float k[N], o[N], s[N], mk = 1.0e-6f, mo = 1.0e-6f;
        for (int i = 0; i < N; ++i)
        {
            k[i] = ctx.d.kick[(size_t) i].load(); o[i] = ctx.d.out[(size_t) i].load(); s[i] = ctx.d.spaceCut[(size_t) i].load();
            mk = juce::jmax (mk, k[i]); mo = juce::jmax (mo, o[i]);
        }
        const float bw = p.getWidth() / N;
        for (int i = 0; i < N; ++i)
        {
            const float x = p.getX() + i * bw;
            const float hk = std::sqrt (k[i] / mk) * p.getHeight() * 0.45f;
            const float ho = std::sqrt (o[i] / mo) * p.getHeight() * 0.45f;
            g.setColour (C::kick.withAlpha (0.35f));
            g.fillRect (x, p.getCentreY() - hk, bw + 0.5f, hk);
            g.setColour (C::bass.withAlpha (0.35f));
            g.fillRect (x, p.getCentreY(), bw + 0.5f, ho);
            const float cut = juce::jlimit (0.0f, 1.0f, -s[i] / 60.0f);
            if (cut > 0.01f)
            {
                g.setColour (C::red.withAlpha (0.25f + 0.7f * cut));
                g.fillRect (x, p.getY(), bw + 0.5f, p.getHeight());
            }
        }
        const float ph = ctx.d.playhead.load();
        if (ph >= 0 && ph < 1)
        {
            g.setColour (C::text.withAlpha (0.4f));
            g.drawVerticalLine (juce::roundToInt (p.getX() + ph * p.getWidth()), p.getY(), p.getBottom());
        }
    }
private:
    Ctx& ctx;
};

class SpacePage : public juce::Component
{
public:
    explicit SpacePage (Ctx& c) : ctx (c), bells (c), timeline (c)
    {
        addAndMakeVisible (bells);
        addAndMakeVisible (timeline);
        addAndMakeVisible (on);      on.attach (ctx.p (ID::spaceOn));
        addAndMakeVisible (amount);  amount.attach (ctx.s, ID::spaceAmount);
        addAndMakeVisible (release); release.attach (ctx.s, ID::spaceRelease);
        release.valueText = [this] { return juce::String (juce::roundToInt (ctx.v (ID::spaceRelease))) + " ms"; };
    }
    void resized() override
    {
        bells.setBounds (0, 0, getWidth(), 280);
        timeline.setBounds (0, 290, getWidth(), 130);
        const int y = 432, cx = getWidth() / 2;
        on.setBounds (cx - 200, y + 58, 76, 32);
        amount.setBounds (cx - 90, y + 6, 112, 136);
        release.setBounds (cx + 40, y + 6, 112, 136);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = juce::Rectangle<float> (0, 432, (float) getWidth(), (float) getHeight() - 432);
        panelBox (g, r);
        label (g, "SPACE", { (float) getWidth() / 2 - 200, r.getY() + 30, 76, 18 }, C::dim, 11.0f, juce::Justification::centred);
        g.setFont (font (11.5f, false, 0.02f));
        g.setColour (C::dim);
        g.drawFittedText ("Narrow cuts on the bass, only in the bands where the kick is loud and the bass has energy, "
                          "and only while the kick sounds. RELEASE sets how fast each bell lets go.",
                          juce::Rectangle<int> (getWidth() / 2 + 190, (int) r.getY() + 40, getWidth() / 2 - 210, 70), juce::Justification::topLeft, 4, 1.0f);
        const bool on_ = ctx.v (ID::spaceOn) > 0.5f;
        if (! on_) label (g, "SPACE IS OFF", { 24, r.getY() + 16, 200, 18 }, C::faint, 11.0f);
    }
    void sync() { bells.repaint(); timeline.repaint(); }
    Ctx& ctx;
    BellsView bells;
    SpaceTimeline timeline;
    Pill on { "ON" };
    Knob amount { "AMOUNT" }, release { "RELEASE" };
};

//==============================================================================
class Header : public juce::Component
{
public:
    explicit Header (Ctx& c) : ctx (c)
    {
        addAndMakeVisible (rate);   rate.attach (ctx.p (ID::rate));
        addAndMakeVisible (status); status.attach (ctx.p (ID::gridOn));
        addAndMakeVisible (bypass); bypass.attach (ctx.p (ID::bypass));
        bypass.solidWhenOn = true;
        status.textFn = [this]
        {
            switch (ctx.d.status.load())
            {
                case DisplayData::routed: return juce::String ("KICK " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " ROUTED");
                case DisplayData::sample: return juce::String ("SAMPLE " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " ON GRID");
                case DisplayData::hold:   return juce::String ("HOLD " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " LAST KICK");
                case DisplayData::gridOff: return juce::String ("GRID OFF");
                default: return juce::String ("GRID " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " NO KICK");
            }
        };
    }
    void resized() override
    {
        presetArea = { 300, 14, 300, 44 };
        rate.setBounds (614, 14, 270, 44);
        status.setBounds (898, 14, 176, 44);
        bypass.setBounds (1088, 14, 88, 44);
    }
    void paint (juce::Graphics& g) override
    {
        // logo: a wave pulled under by the kick
        juce::Path w;
        const float x0 = 26, y0 = 36;
        w.startNewSubPath (x0, y0 - 9);
        w.cubicTo (x0 + 6, y0 - 9, x0 + 6, y0 + 10, x0 + 13, y0 + 10);
        w.cubicTo (x0 + 22, y0 + 10, x0 + 20, y0 - 9, x0 + 34, y0 - 9);
        g.setColour (C::accent.withAlpha (0.3f));
        g.strokePath (w, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (C::accent);
        g.strokePath (w, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (font (24.0f, true, 0.12f));
        g.setColour (C::text);
        g.drawText ("UNDER", 72, 20, 100, 32, juce::Justification::centredLeft);
        const float uw = juce::GlyphArrangement::getStringWidth (font (24.0f, true, 0.12f), "UNDER");
        g.setColour (C::accent);
        g.drawText ("TOW", 72 + (int) uw, 20, 80, 32, juce::Justification::centredLeft);

        // preset selector
        auto r = presetArea.toFloat().reduced (0.5f);
        g.setColour (presetHover ? C::raised : C::panel2);
        g.fillRoundedRectangle (r, 8);
        g.setColour (C::line);
        g.drawRoundedRectangle (r, 8, 1);
        const auto name = ctx.proc.getProgramName (0).toUpperCase();
        label (g, "PRESET", r.withTrimmedLeft (16), C::dim, 12.0f);
        label (g, name, r.withTrimmedLeft (84).withTrimmedRight (30), C::text, 12.5f, juce::Justification::centredLeft, true, 0.06f);
        label (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")), r.removeFromRight (30), C::dim, 13.0f, juce::Justification::centred);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool h = presetArea.contains (e.getPosition());
        if (h != presetHover) { presetHover = h; repaint (presetArea); }
    }
    void mouseExit (const juce::MouseEvent&) override { presetHover = false; repaint (presetArea); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! presetArea.contains (e.getPosition())) return;
        juce::PopupMenu m;
        m.addSectionHeader ("FACTORY PRESETS");
        const auto presets = getFactoryPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            m.addItem (i + 1, presets[(size_t) i].name, true, i == ctx.proc.currentPreset);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (presetArea)),
                         [this] (int r) { if (r > 0) { ctx.proc.applyPreset (r - 1); repaint(); } });
    }
    void sync() { status.repaint(); repaint (presetArea); }

    Ctx& ctx;
    juce::Rectangle<int> presetArea;
    bool presetHover = false;
    Segmented rate { { "1/16", "1/8", "1/4", "1/2", "1 BAR" } };
    Pill status { "GRID" };
    Pill bypass { "BYPASS", false };
};

//==============================================================================
class Root : public juce::Component, private juce::Timer
{
public:
    explicit Root (UndertowProcessor& p)
        : ctx (p), header (ctx), sidechain (ctx), phase (ctx), space (ctx)
    {
        addAndMakeVisible (header);
        addAndMakeVisible (tabs);
        addAndMakeVisible (undoBtn);
        addAndMakeVisible (redoBtn);
        addChildComponent (sidechain);
        addChildComponent (phase);
        addChildComponent (space);
        tabs.textSize = 12.5f;
        tabs.onChange = [this] (int i) { ctx.proc.uiState.setProperty ("tab", i, nullptr); showTab (i); };
        const int t = (int) ctx.proc.uiState["tab"];
        tabs.setSelected (t, false);
        showTab (t);
        undoBtn.onClick = [this] { doUndo (true); };
        redoBtn.onClick = [this] { doUndo (false); };
        last = ctx.proc.snapshot();
        updateUndoButtons();
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    void showTab (int i)
    {
        sidechain.setVisible (i == 0);
        phase.setVisible (i == 1);
        space.setVisible (i == 2);
        repaint();
    }

    void resized() override
    {
        header.setBounds (0, 0, getWidth(), 70);
        tabs.setBounds (24, 78, 390, 40);
        undoBtn.setBounds (getWidth() - 106, 80, 38, 36);
        redoBtn.setBounds (getWidth() - 62, 80, 38, 36);
        const auto page = juce::Rectangle<int> (24, 130, getWidth() - 48, getHeight() - 130 - 16);
        sidechain.setBounds (page);
        phase.setBounds (page);
        space.setBounds (page);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (C::bg);
        static const char* subs[] = { "ONE CYCLE OF BASS LEVEL  \xc2\xb7  EVERY KICK STARTS IT OVER",
                                      "THE SUB ROTATED SO IT CARRIES ON FROM THE KICK",
                                      "SHORT CUTS WHERE KICK AND BASS COLLIDE" };
        label (g, juce::String (juce::CharPointer_UTF8 (subs[juce::jlimit (0, 2, tabs.getSelected())])), { 432, 78, 600, 40 }, C::dim, 11.0f);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { doUndo (true); return true; }
        if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
            || k == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)) { doUndo (false); return true; }
        return false;
    }

    Ctx ctx;

private:
    void timerCallback() override
    {
        header.sync();
        if (sidechain.isVisible()) { sidechain.sync(); sidechain.curveView.repaint(); }
        ctx.d.phaseView.store (phase.isVisible());
        ctx.d.spaceView.store (space.isVisible());
        if (phase.isVisible()) phase.sync();
        if (space.isVisible()) space.sync();

        // undo history: take a snapshot whenever an edit settles
        if (++undoTick % 8 == 0 && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        {
            const auto now = ctx.proc.snapshot();
            if (now != last)
            {
                undo.push_back (last);
                if (undo.size() > 100) undo.erase (undo.begin());
                redo.clear();
                last = now;
                updateUndoButtons();
            }
        }
    }

    void doUndo (bool isUndo)
    {
        auto& from = isUndo ? undo : redo;
        auto& to = isUndo ? redo : undo;
        if (from.empty()) return;
        to.push_back (ctx.proc.snapshot());
        const auto s = from.back();
        from.pop_back();
        ctx.proc.restoreSnapshot (s);
        last = ctx.proc.snapshot();
        updateUndoButtons();
        repaint();
    }
    void updateUndoButtons()
    {
        undoBtn.setEnabled (! undo.empty()); redoBtn.setEnabled (! redo.empty());
        undoBtn.repaint(); redoBtn.repaint();
    }

    Header header;
    Segmented tabs { { "SIDECHAIN", "PHASE", "SPACE" } };
    IconButton undoBtn { IconButton::undo }, redoBtn { IconButton::redo };
    SidechainPage sidechain;
    PhasePage phase;
    SpacePage space;
    std::vector<juce::String> undo, redo;
    juce::String last;
    int undoTick = 0;
};

//==============================================================================
UndertowEditor::UndertowEditor (UndertowProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    root = std::make_unique<Root> (p);
    addAndMakeVisible (*root);
    setResizable (true, true);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / baseH);
    const double scale = (double) p.uiState.getProperty ("scale", 1.0);
    setSize (juce::roundToInt (baseW * scale), juce::roundToInt (baseH * scale));
}

UndertowEditor::~UndertowEditor()
{
    proc.display.phaseView.store (false);
    proc.display.spaceView.store (false);
    root = nullptr;
    setLookAndFeel (nullptr);
}

void UndertowEditor::paint (juce::Graphics& g) { g.fillAll (C::bg); }

void UndertowEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    root->setBounds (0, 0, baseW, baseH);
    root->setTransform (juce::AffineTransform::scale (s));
    proc.uiState.setProperty ("scale", s, nullptr);
}

bool UndertowEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}
void UndertowEditor::fileDragEnter (const juce::StringArray&, int, int) { root->ctx.dragOver = true; root->repaint(); }
void UndertowEditor::fileDragExit (const juce::StringArray&) { root->ctx.dragOver = false; root->repaint(); }
void UndertowEditor::filesDropped (const juce::StringArray& files, int, int)
{
    root->ctx.dragOver = false;
    for (auto& f : files)
        if (isAudioFile (f) && proc.loadKickFile (juce::File (f))) break;
    root->repaint();
}
