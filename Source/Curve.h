#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <algorithm>

// A ducking curve over one cycle: x in [0,1] (time from the kick), y in [0,1] (bass level).
// Points are kept sorted by x (equal x allowed -> vertical step). Each segment has a bend in [-1,1].
struct Curve
{
    struct Point { float x = 0, y = 0; };

    std::vector<Point> pts;
    std::vector<float> bend;   // size = pts.size() - 1

    static float bendExponent (float b)            { return std::exp (-b * 2.5f); }
    static float shape (float u, float b)
    {
        u = juce::jlimit (0.0f, 1.0f, u);
        if (std::abs (b) < 1.0e-4f) return u;
        return std::pow (u, bendExponent (b));
    }
    // bend value that makes the segment pass through fraction v at its midpoint
    static float bendForMidValue (float v)
    {
        v = juce::jlimit (0.01f, 0.99f, v);
        const float p = std::log (v) / std::log (0.5f);
        return juce::jlimit (-1.0f, 1.0f, -std::log (p) / 2.5f);
    }

    void normalise()
    {
        if (pts.size() < 2) { pts = { { 0, 0 }, { 1, 1 } }; }
        std::stable_sort (pts.begin(), pts.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
        for (auto& p : pts) { p.x = juce::jlimit (0.0f, 1.0f, p.x); p.y = juce::jlimit (0.0f, 1.0f, p.y); }
        pts.front().x = 0.0f;
        pts.back().x  = 1.0f;
        bend.resize (pts.size() - 1, 0.0f);
        for (auto& b : bend) b = juce::jlimit (-1.0f, 1.0f, b);
    }

    float eval (float x) const
    {
        if (pts.empty()) return 1.0f;
        if (x <= pts.front().x) return pts.front().y;
        if (x >= pts.back().x)  return pts.back().y;
        // last segment whose start is <= x
        size_t i = 0;
        for (size_t k = 0; k + 1 < pts.size(); ++k)
            if (pts[k].x <= x && x < pts[k + 1].x) { i = k; break; }
        const auto& a = pts[i];
        const auto& b = pts[i + 1];
        const float w = b.x - a.x;
        if (w <= 1.0e-6f) return b.y;
        const float u = shape ((x - a.x) / w, bend[i]);
        return a.y + (b.y - a.y) * u;
    }

    void fillTable (float* t, int n) const
    {
        for (int i = 0; i < n; ++i)
            t[i] = eval ((float) i / (float) (n - 1));
    }

    // value at which the curve first reaches "open" (>= thr); returns 1.0 if never
    float openAt (float thr = 0.98f) const
    {
        const int n = 2000;
        for (int i = 0; i <= n; ++i)
        {
            const float x = (float) i / n;
            if (eval (x) >= thr) return x;
        }
        return 1.0f;
    }

    juce::String toString() const
    {
        juce::StringArray a;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const float b = i < bend.size() ? bend[i] : 0.0f;
            a.add (juce::String (pts[i].x, 5) + "," + juce::String (pts[i].y, 5) + "," + juce::String (b, 4));
        }
        return a.joinIntoString (";");
    }

    static Curve fromString (const juce::String& s)
    {
        Curve c;
        juce::StringArray items;
        items.addTokens (s, ";", "");
        for (auto& it : items)
        {
            juce::StringArray v;
            v.addTokens (it, ",", "");
            if (v.size() >= 2)
            {
                c.pts.push_back ({ v[0].getFloatValue(), v[1].getFloatValue() });
                c.bend.push_back (v.size() >= 3 ? v[2].getFloatValue() : 0.0f);
            }
        }
        if (! c.bend.empty()) c.bend.pop_back();
        c.normalise();
        return c;
    }

    bool operator== (const Curve& o) const
    {
        if (pts.size() != o.pts.size()) return false;
        for (size_t i = 0; i < pts.size(); ++i)
            if (pts[i].x != o.pts[i].x || pts[i].y != o.pts[i].y) return false;
        return bend == o.bend;
    }
    bool operator!= (const Curve& o) const { return ! (*this == o); }

    // helper for building shapes: list of {x,y,bendOfSegmentStartingHere}
    static Curve make (std::initializer_list<std::array<float, 3>> list)
    {
        Curve c;
        for (auto& p : list) { c.pts.push_back ({ p[0], p[1] }); c.bend.push_back (p[2]); }
        c.bend.pop_back();
        c.normalise();
        return c;
    }
};

struct Shapes
{
    struct Shape { juce::String id, name; Curve curve; bool user = false; };

    static std::vector<Shape> builtIn()
    {
        constexpr float e = 0.004f; // tiny width for vertical steps
        return {
            { "steps",   "Steps",      Curve::make ({ {0,0,0}, {0.25f,0,0}, {0.25f+e,0.5f,0}, {0.5f,0.5f,0}, {0.5f+e,1,0}, {1,1,0} }) },
            { "gate",    "Gate",       Curve::make ({ {0,0,0}, {0.25f,0,0}, {0.25f+e,1,0}, {1,1,0} }) },
            { "fast",    "Fast",       Curve::make ({ {0,0,0.85f}, {0.45f,1,0}, {1,1,0} }) },
            { "hold",    "Hold",       Curve::make ({ {0,0,0}, {0.12f,0,0.7f}, {0.45f,1,0}, {1,1,0} }) },
            { "soft",    "Soft step",  Curve::make ({ {0,0,0}, {0.2f,0,-0.6f}, {0.32f,0.55f,0.6f}, {0.45f,1,0}, {1,1,0} }) },
            { "snap",    "Snap",       Curve::make ({ {0,0,0}, {0.06f,1,0}, {1,1,0} }) },
            { "ramp",    "Ramp",       Curve::make ({ {0,0,0}, {1,1,0} }) },
            { "sigmoid", "S-curve",    Curve::make ({ {0,0,-0.7f}, {0.5f,0.5f,0.7f}, {1,1,0} }) },
            { "expo",    "Expo",       Curve::make ({ {0,0,-0.8f}, {1,1,0} }) },
            { "log",     "Log",        Curve::make ({ {0,0,0.7f}, {1,1,0} }) },
            { "bounce",  "Bounce",     Curve::make ({ {0,0,0.7f}, {0.3f,1,0}, {0.47f,1,0.8f}, {0.5f,0.15f,0.6f}, {0.62f,1,0}, {1,1,0} }) },
            { "dip",     "Dip",        Curve::make ({ {0,0,0.8f}, {0.22f,0.95f,0.3f}, {0.38f,0.7f,-0.4f}, {0.55f,1,0}, {1,1,0} }) },
            { "breathe", "Breathe",    Curve::make ({ {0,0,0.85f}, {0.7f,0.85f,0.3f}, {1,1,0} }) },
        };
    }
};
