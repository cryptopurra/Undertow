#pragma once
#include <JuceHeader.h>
#include <vector>
#include <array>
#include "../third_party/pffft/pffft.h"

// Uniformly-partitioned FFT convolution of 3 inputs (bass L, bass R, kick) against a bank of
// linear-phase low-pass kernels and their Hilbert transforms. Every output shares the same fixed
// latency D = P + c (c = the kernels' centre tap), so the bands always add back to the input.
// Kernel changes crossfade over one partition: moving a crossover never clicks.
class LinearPhaseBank
{
public:
    enum Kernel { kLs, kHLs, kLlo, kHLlo, kLhi, kHLhi, numKernels };
    static constexpr int numInputs = 3;
    static constexpr int numSlots  = 2 * numKernels + 2;   // bass L(6), bass R(6), kick(Ls, HLs)

    static int slot (int input, int kernel)
    {
        return input < 2 ? input * numKernels + kernel : 2 * numKernels + kernel;
    }

    LinearPhaseBank() = default;
    ~LinearPhaseBank() { if (setup != nullptr) pffft_destroy_setup (setup); }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        int scale = 1;
        while (fs / scale > 64000.0) scale *= 2;
        P = 512 * scale;
        if (setup != nullptr) pffft_destroy_setup (setup);
        setup = pffft_new_setup (2 * P, PFFFT_REAL);

        const int target = (int) std::ceil (0.055 * fs);                // ~55-64 ms
        D = ((target + P - 1) / P) * P;
        c = D - P;
        N = 2 * c + 1;
        K = (N + P - 1) / P;
        F = 2 * P;

        window.resize ((size_t) N);
        const double beta = 5.0;
        auto i0 = [] (double x)
        {
            double sum = 1, term = 1;
            for (int k = 1; k < 60; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; if (term < 1e-12 * sum) break; }
            return sum;
        };
        const double den = i0 (beta);
        for (int n = 0; n < N; ++n)
        {
            const double r = (2.0 * n) / (N - 1) - 1.0;
            window[(size_t) n] = (float) (i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / den);
        }

        for (auto& s : sets)
        {
            for (auto& k : s.H) k.allocate ((size_t) (K * F));
            s.enabled.fill (false);
        }
        for (int ch = 0; ch < numInputs; ++ch)
        {
            inBuf[ch].assign ((size_t) (2 * P), 0.0f);
            fdl[ch].allocate ((size_t) (K * F));
        }
        for (auto& o : outBlock) o.assign ((size_t) P, 0.0f);
        tIn.allocate ((size_t) F); tOut.allocate ((size_t) F); work.allocate ((size_t) F); acc.allocate ((size_t) F);
        tmpOld.assign ((size_t) P, 0.0f);
        hBuf.assign ((size_t) (K * P), 0.0f);
        hhBuf.assign ((size_t) (K * P), 0.0f);
        pos = 0; fdlWrite = 0; crossfading = false; active = 0;
        designSet (sets[0], wanted[0], wanted[1], wanted[2]);
        designed = wanted;
    }

    int getLatency() const   { return D; }
    int getPartition() const { return P; }

    // edges in Hz; <= 0 disables that pair of kernels
    void setEdges (float split, float lo, float hi)  { wanted = { split, lo, hi }; }
    // which output slots are needed (one bit per slot); the others output zeros
    void setActiveSlots (juce::uint32 mask)           { activeMask = mask; }

    // in: 3 inputs at time n; out: numSlots values at time n - D
    inline void process (const float* in, float* out)
    {
        for (int s = 0; s < numSlots; ++s) out[s] = outBlock[(size_t) s][(size_t) pos];
        for (int ch = 0; ch < numInputs; ++ch) inBuf[ch][(size_t) (P + pos)] = in[ch];
        if (++pos == P) { pos = 0; processPartition(); }
    }

private:
    struct Aligned
    {
        float* p = nullptr;
        size_t n = 0;
        ~Aligned() { if (p != nullptr) pffft_aligned_free (p); }
        void allocate (size_t size)
        {
            if (p != nullptr) pffft_aligned_free (p);
            p = (float*) pffft_aligned_malloc (size * sizeof (float));
            n = size;
            std::fill (p, p + n, 0.0f);
        }
        float* data() const { return p; }
    };

    struct KernelSet
    {
        std::array<Aligned, numKernels> H;   // [partition][F] in pffft's internal order
        std::array<bool, numKernels> enabled {};
    };

    static bool differs (float a, float b)
    {
        if (a <= 0 && b <= 0) return false;
        if ((a <= 0) != (b <= 0)) return true;
        return std::abs (a - b) > 0.25f;
    }

    void designLP (float fc, float* h, float* hh)
    {
        std::fill (h,  h  + K * P, 0.0f);
        std::fill (hh, hh + K * P, 0.0f);
        const double wc = juce::MathConstants<double>::twoPi * juce::jlimit (1.0, 0.45 * fs, (double) fc) / fs;
        double sum = 0;
        for (int n = 0; n < N; ++n)
        {
            const double t = n - c;
            double v, vh;
            if (t == 0) { v = wc / juce::MathConstants<double>::pi; vh = 0; }
            else
            {
                v  = std::sin (wc * t) / (juce::MathConstants<double>::pi * t);
                vh = (1.0 - std::cos (wc * t)) / (juce::MathConstants<double>::pi * t);
            }
            h[n]  = (float) (v * window[(size_t) n]);
            hh[n] = (float) (vh * window[(size_t) n]);
            sum += h[n];
        }
        const float g = (float) (1.0 / sum);
        for (int n = 0; n < N; ++n) { h[n] *= g; hh[n] *= g; }
    }

    void toPartitions (const float* h, Aligned& H)
    {
        for (int k = 0; k < K; ++k)
        {
            std::fill (tIn.data(), tIn.data() + F, 0.0f);
            std::copy (h + k * P, h + (k + 1) * P, tIn.data());
            pffft_transform (setup, tIn.data(), H.data() + k * F, work.data(), PFFFT_FORWARD);
        }
    }

    void designSet (KernelSet& s, float split, float lo, float hi)
    {
        const float edges[3] = { split, lo, hi };
        for (int e = 0; e < 3; ++e)
        {
            const bool on = edges[e] > 0;
            s.enabled[(size_t) (2 * e)] = on;
            s.enabled[(size_t) (2 * e + 1)] = on;
            if (! on) continue;
            designLP (edges[e], hBuf.data(), hhBuf.data());
            toPartitions (hBuf.data(),  s.H[(size_t) (2 * e)]);
            toPartitions (hhBuf.data(), s.H[(size_t) (2 * e + 1)]);
        }
    }

    void convolveInverse (int ch, const KernelSet& s, int kernel, float* dest)
    {
        std::fill (acc.data(), acc.data() + F, 0.0f);
        const float scale = 1.0f / (float) F;
        for (int k = 0; k < K; ++k)
        {
            const int idx = (fdlWrite - k + K) % K;
            pffft_zconvolve_accumulate (setup, fdl[ch].data() + idx * F, s.H[(size_t) kernel].data() + k * F, acc.data(), scale);
        }
        pffft_transform (setup, acc.data(), tOut.data(), work.data(), PFFFT_BACKWARD);
        std::copy (tOut.data() + P, tOut.data() + 2 * P, dest);
    }

    void processPartition()
    {
        fdlWrite = (fdlWrite + 1) % K;
        for (int ch = 0; ch < numInputs; ++ch)
        {
            std::copy (inBuf[ch].begin(), inBuf[ch].end(), tIn.data());
            pffft_transform (setup, tIn.data(), fdl[ch].data() + fdlWrite * F, work.data(), PFFFT_FORWARD);
            std::copy (inBuf[ch].begin() + P, inBuf[ch].end(), inBuf[ch].begin());
        }

        if (crossfading) { crossfading = false; active = 1 - active; }

        if (differs (wanted[0], designed[0]) || differs (wanted[1], designed[1]) || differs (wanted[2], designed[2]))
        {
            designSet (sets[(size_t) (1 - active)], wanted[0], wanted[1], wanted[2]);
            designed = wanted;
            crossfading = true;
        }

        const auto& cur = sets[(size_t) (crossfading ? 1 - active : active)];
        const auto& old = sets[(size_t) active];

        for (int s = 0; s < numSlots; ++s)
        {
            const int ch = s < 2 * numKernels ? s / numKernels : 2;
            const int kernel = s < 2 * numKernels ? s % numKernels : s - 2 * numKernels;
            auto& dst = outBlock[(size_t) s];
            const bool needed = (activeMask >> s) & 1u;

            if (needed && cur.enabled[(size_t) kernel]) convolveInverse (ch, cur, kernel, dst.data());
            else std::fill (dst.begin(), dst.end(), 0.0f);

            if (crossfading && needed)
            {
                if (old.enabled[(size_t) kernel]) convolveInverse (ch, old, kernel, tmpOld.data());
                else std::fill (tmpOld.begin(), tmpOld.end(), 0.0f);
                for (int i = 0; i < P; ++i)
                {
                    const float t = (i + 0.5f) / (float) P;
                    dst[(size_t) i] = tmpOld[(size_t) i] * (1.0f - t) + dst[(size_t) i] * t;
                }
            }
        }
    }

    double fs = 48000;
    int P = 512, D = 3072, c = 2560, N = 5121, K = 11, F = 1024;
    PFFFT_Setup* setup = nullptr;
    std::vector<float> window, tmpOld, hBuf, hhBuf;
    std::array<KernelSet, 2> sets;
    int active = 0;
    bool crossfading = false;
    std::array<float, 3> wanted { 90.0f, -1.0f, -1.0f }, designed { -1, -1, -1 };
    std::array<std::vector<float>, numInputs> inBuf;
    std::array<Aligned, numInputs> fdl;
    std::array<std::vector<float>, numSlots> outBlock;
    Aligned tIn, tOut, work, acc;
    juce::uint32 activeMask = 0xffffffffu;
    int pos = 0, fdlWrite = 0;
};
