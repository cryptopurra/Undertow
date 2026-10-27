#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>

// "Space": a dynamic EQ on the bass. A row of narrow bells between lo and hi; each bell cuts
// only while the kick is loud in that band AND the bass has content there.
class SpaceEQ
{
public:
    static constexpr int numBands = 20;
    static constexpr int maxCh = 2;
    static constexpr float maxCutDb = 24.0f;

    struct Bell   // Simper TPT state-variable bell
    {
        float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, m1 = 0;
        float ic1 = 0, ic2 = 0;
        void reset() { ic1 = ic2 = 0; }
        void set (double fs, double f, double q, float gainLin)
        {
            const float A = std::sqrt (gainLin);
            g = (float) std::tan (juce::MathConstants<double>::pi * f / fs);
            k = (float) (1.0 / (q * A));
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
            m1 = k * (A * A - 1.0f);
        }
        inline float tick (float v0)
        {
            const float v3 = v0 - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v0 + m1 * v1;
        }
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int i = 0; i < numBands; ++i)
        {
            kic1[i] = kic2[i] = bic1[i] = bic2[i] = 0;
            kEnv[i] = bEnv[i] = 0;
            gainDb[i] = 0; appliedDb[i] = 1000; active[i] = false;
            for (auto& bl : bells[(size_t) i]) bl.reset();
        }
        attK = coef (1.0); relK = coef (15.0);
        cutAtt = coef (2.0);
        peakDecay16 = (float) std::exp (-16.0 / (1.5 * fs));
        kPeak = 0;
        counter = 0;
        lo = hi = -1;
        setParams (false, 0.3f, 30.0f);
        setRange (20, 250);
    }

    void setRange (float newLo, float newHi)
    {
        newHi = std::max (newHi, newLo * 1.5f);
        if (std::abs (newLo - lo) < 0.1f && std::abs (newHi - hi) < 0.1f) return;
        lo = newLo; hi = newHi;
        const double bwOct = std::log2 (hi / lo) / numBands;
        const double q = juce::jlimit (2.0, 12.0, 1.2 / (2.0 * std::sinh (std::log (2.0) / 2.0 * bwOct)));
        for (int i = 0; i < numBands; ++i)
        {
            freq[i] = lo * std::pow (hi / lo, (i + 0.5f) / numBands);
            qs[i] = (float) q;
            const double g = std::tan (juce::MathConstants<double>::pi * freq[i] / fs), k = 1.0 / q;
            a1[i] = (float) (1.0 / (1.0 + g * (g + k)));
            a2[i] = (float) g * a1[i];
            a3[i] = (float) g * a2[i];
            appliedDb[i] = 1000;
        }
    }

    bool isIdle() const
    {
        for (int i = 0; i < numBands; ++i) if (gainDb[i] < -0.01f) return false;
        return true;
    }

    void setParams (bool on, float amount01, float releaseMs)
    {
        enabled = on; amount = amount01;
        cutRel = coef (releaseMs);
        att16 = 1.0f - std::pow (1.0f - cutAtt, 16.0f);
        rel16 = 1.0f - std::pow (1.0f - cutRel, 16.0f);
    }

    // kick = kick reference (mono), bassMono = bass as heard; io = bass channels, filtered in place
    inline float process (float kick, float bassMono, float* io, int numCh)
    {
        // band detectors (structure of arrays so the compiler can vectorise)
        for (int i = 0; i < numBands; ++i)
        {
            const float kv3 = kick - kic2[i];
            const float kv1 = a1[i] * kic1[i] + a2[i] * kv3;
            const float kv2 = kic2[i] + a2[i] * kic1[i] + a3[i] * kv3;
            kic1[i] = 2.0f * kv1 - kic1[i];
            kic2[i] = 2.0f * kv2 - kic2[i];
            const float ka = std::abs (kv1);
            kEnv[i] += (ka > kEnv[i] ? attK : relK) * (ka - kEnv[i]);

            const float bv3 = bassMono - bic2[i];
            const float bv1 = a1[i] * bic1[i] + a2[i] * bv3;
            const float bv2 = bic2[i] + a2[i] * bic1[i] + a3[i] * bv3;
            bic1[i] = 2.0f * bv1 - bic1[i];
            bic2[i] = 2.0f * bv2 - bic2[i];
            const float ba = std::abs (bv1);
            bEnv[i] += (ba > bEnv[i] ? attK : relK) * (ba - bEnv[i]);
        }

        if (++counter >= 16)
        {
            counter = 0;
            float kMax = 1.0e-9f, bMax = 1.0e-9f;
            for (int i = 0; i < numBands; ++i) { kMax = std::max (kMax, kEnv[i]); bMax = std::max (bMax, bEnv[i]); }
            // the kick's own loudest moment is the reference: its quiet tail does not count as "loud"
            kPeak = std::max (kMax, kPeak * peakDecay16);
            const bool kickOn = kMax > 2.0e-3f && kMax > 0.1f * kPeak;
            for (int i = 0; i < numBands; ++i)
            {
                float target = 0;
                if (enabled && kickOn && bEnv[i] > 1.0e-4f)
                {
                    const float w = smooth (0.15f, 0.6f, kEnv[i] / kPeak) * smooth (0.08f, 0.5f, bEnv[i] / bMax);
                    target = -amount * maxCutDb * w;
                }
                gainDb[i] += (target < gainDb[i] ? att16 : rel16) * (target - gainDb[i]);
                if (std::abs (gainDb[i] - appliedDb[i]) > 0.05f)
                {
                    const float lin = juce::Decibels::decibelsToGain (gainDb[i]);
                    for (auto& bl : bells[(size_t) i]) bl.set (fs, freq[i], qs[i], lin);
                    appliedDb[i] = gainDb[i];
                }
            }
        }

        float totalCut = 0;
        for (int i = 0; i < numBands; ++i)
        {
            const bool act = gainDb[i] < -0.05f;
            if (act && ! active[i]) for (auto& bl : bells[(size_t) i]) bl.reset();
            active[i] = act;
            if (! act) continue;
            totalCut += gainDb[i];
            for (int ch = 0; ch < numCh; ++ch) io[ch] = bells[(size_t) i][(size_t) ch].tick (io[ch]);
        }
        return totalCut;   // sum of dB (<= 0)
    }

    // read by the display
    float freq[numBands] {}, qs[numBands] {}, kEnv[numBands] {}, bEnv[numBands] {}, gainDb[numBands] {};
    float lo = 20, hi = 250;

private:
    float coef (double ms) const { return (float) (1.0 - std::exp (-1.0 / (0.001 * ms * fs))); }
    static float smooth (float e0, float e1, float x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0));
        return t * t * (3 - 2 * t);
    }

    double fs = 48000;
    bool enabled = false;
    float amount = 0.3f;
    float attK = 0, relK = 0, cutAtt = 0, cutRel = 0, att16 = 0, rel16 = 0, kPeak = 0, peakDecay16 = 1;
    int counter = 0;
    alignas (16) float a1[numBands] {}, a2[numBands] {}, a3[numBands] {};
    alignas (16) float kic1[numBands] {}, kic2[numBands] {}, bic1[numBands] {}, bic2[numBands] {};
    float appliedDb[numBands] {};
    bool active[numBands] {};
    std::array<std::array<Bell, maxCh>, numBands> bells;
};
