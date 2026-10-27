// Offline tests for Undertow's DSP + editor screenshots. Linux only, run under xvfb-run.
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

static constexpr double FS = 48000.0;
static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static std::vector<float> synthKick (int len)
{
    std::vector<float> k ((size_t) len);
    double ph = 0;
    for (int i = 0; i < len; ++i)
    {
        const double t = i / FS;
        const double f = 48 + 160 * std::exp (-t / 0.025);
        ph += juce::MathConstants<double>::twoPi * f / FS;
        k[(size_t) i] = (float) (0.9 * std::sin (ph) * std::exp (-t / 0.12) * (t < 0.3 ? 1 : 0));
    }
    return k;
}

struct Rig
{
    std::unique_ptr<UndertowProcessor> p = std::make_unique<UndertowProcessor>();
    int bs = 480;
    int D = 0;

    explicit Rig (bool sidechain, std::function<void (UndertowProcessor&)> setup = {})
    {
        auto layout = p->getBusesLayout();
        if (! sidechain) layout.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();
        p->setBusesLayout (layout);
        if (setup) setup (*p);
        p->prepareToPlay (FS, bs);
        D = p->getLatencySamples();
    }
    void set (const juce::String& id, float v)
    {
        auto* rp = p->apvts.getParameter (id);
        rp->setValueNotifyingHost (rp->convertTo0to1 (v));
    }
    // bass stereo (L=R), kick mono -> output L
    std::vector<float> run (const std::vector<float>& bass, const std::vector<float>& kick)
    {
        const int N = (int) bass.size();
        std::vector<float> out ((size_t) N);
        const int nIn = p->getTotalNumInputChannels();
        juce::AudioBuffer<float> buf (juce::jmax (nIn, 2), bs);
        juce::MidiBuffer midi;
        for (int s = 0; s < N; s += bs)
        {
            const int n = juce::jmin (bs, N - s);
            buf.setSize (buf.getNumChannels(), n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, bass[(size_t) (s + i)]);
                buf.setSample (1, i, bass[(size_t) (s + i)]);
                if (nIn >= 4) { buf.setSample (2, i, kick[(size_t) (s + i)]); buf.setSample (3, i, kick[(size_t) (s + i)]); }
            }
            p->processBlock (buf, midi);
            for (int i = 0; i < n; ++i) out[(size_t) (s + i)] = buf.getSample (0, i);
        }
        return out;
    }
};

static double rms (const std::vector<float>& v, int a, int b)
{
    double s = 0; a = juce::jmax (0, a); b = juce::jmin ((int) v.size(), b);
    for (int i = a; i < b; ++i) s += (double) v[(size_t) i] * v[(size_t) i];
    return b > a ? std::sqrt (s / (b - a)) : 0;
}
static double toneAmp (const std::vector<float>& v, int a, int b, double f)
{
    double re = 0, im = 0;
    for (int i = a; i < b; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (i - a) / (b - a));
        re += v[(size_t) i] * w * std::cos (juce::MathConstants<double>::twoPi * f * i / FS);
        im += v[(size_t) i] * w * std::sin (juce::MathConstants<double>::twoPi * f * i / FS);
    }
    return 2.0 * std::sqrt (re * re + im * im) / ((b - a) * 0.5);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const int beat = (int) (FS * 0.5);               // 120 bpm (internal clock)
    const int N = (int) (FS * 6);
    const int kickOffset = 1234;                      // kicks deliberately off the grid
    std::vector<float> kick ((size_t) N, 0.0f), bass ((size_t) N);
    auto ks = synthKick ((int) (0.45 * FS));
    std::vector<int> kickTimes;
    for (int t = kickOffset; t + (int) ks.size() < N; t += beat)
    {
        kickTimes.push_back (t);
        for (size_t i = 0; i < ks.size(); ++i) kick[(size_t) t + i] += ks[i];
    }
    for (int i = 0; i < N; ++i)
        bass[(size_t) i] = (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 55 * i / FS + 0.7)
                                  + 0.2 * std::sin (juce::MathConstants<double>::twoPi * 220 * i / FS));

    auto gate = Shapes::builtIn()[1].curve;   // closed for the first quarter, then open

    std::cout << "\n[1] Sidechain duck, FULL, gate shape" << std::endl;
    {
        Rig r (true);
        r.p->setCurve (gate);
        std::cout << "  latency " << r.D << " samples (" << r.D / FS * 1000 << " ms)" << std::endl;
        auto out = r.run (bass, kick);
        double maxAtKick = 0, closed = 0, open = 0, pre = 0;
        int cnt = 0;
        for (size_t k = 2; k + 1 < kickTimes.size(); ++k)
        {
            const int t = kickTimes[k] + r.D;
            maxAtKick = juce::jmax (maxAtKick, (double) std::abs (out[(size_t) t]));
            closed += rms (out, t + 10, t + beat / 4 - 50);
            open += rms (out, t + beat / 4 + 200, t + beat - 400) / rms (bass, t - r.D + beat / 4 + 200, t - r.D + beat - 400);
            pre += std::abs (out[(size_t) t - 200]) / (std::abs (bass[(size_t) (t - r.D - 200)]) + 1e-9);
            ++cnt;
        }
        std::cout << "  |out| at kick start " << maxAtKick << ", closed rms " << closed / cnt << ", open ratio " << open / cnt << std::endl;
        check (maxAtKick < 0.02, "bass silent at the kick's first sample");
        check (closed / cnt < 0.01, "bass ducked while the curve is closed");
        check (std::abs (open / cnt - 1.0) < 0.02, "bass untouched while the curve is open");
        check (r.p->display.status.load() == DisplayData::routed, "status shows routed kick");
    }

    std::cout << "\n[2] Bypass and depth 0 are bit-transparent (delayed)" << std::endl;
    for (int mode = 0; mode < 2; ++mode)
    {
        Rig r (true, [&] (UndertowProcessor& p)
        {
            auto* rp = p.apvts.getParameter (mode == 0 ? ID::bypass : ID::depth);
            rp->setValueNotifyingHost (mode == 0 ? 1.0f : 0.0f);
        });
        auto out = r.run (bass, kick);
        double md = 0;
        for (int i = r.D + 1000; i < N; ++i) md = juce::jmax (md, (double) std::abs (out[(size_t) i] - bass[(size_t) (i - r.D)]));
        check (md < 1.0e-5, juce::String (mode == 0 ? "bypass" : "depth 0") + " max diff " + juce::String (md));
    }

    std::cout << "\n[3] DUCK = BAND 150 Hz - 20 kHz keeps the 55 Hz sub, ducks the 220 Hz part" << std::endl;
    {
        Rig r (true);
        r.p->setCurve (gate);
        r.set (ID::duck, 2); r.set (ID::bandLo, 150); r.set (ID::bandHi, 20000);
        auto out = r.run (bass, kick);
        const int t = kickTimes[6] + r.D;
        const double a55 = toneAmp (out, t + 300, t + beat / 4 - 300, 55), a220 = toneAmp (out, t + 300, t + beat / 4 - 300, 220);
        std::cout << "  in the duck: 55 Hz " << a55 << " (in 0.5), 220 Hz " << a220 << " (in 0.2)" << std::endl;
        check (a55 > 0.42, "sub passes");
        check (a220 < 0.03, "upper band ducked");
    }
    std::cout << "\n[4] DUCK = SUB ducks below 120 Hz only" << std::endl;
    {
        Rig r (true);
        r.p->setCurve (gate);
        r.set (ID::duck, 1);
        auto out = r.run (bass, kick);
        const int t = kickTimes[6] + r.D;
        const double a55 = toneAmp (out, t + 300, t + beat / 4 - 300, 55), a220 = toneAmp (out, t + 300, t + beat / 4 - 300, 220);
        std::cout << "  in the duck: 55 Hz " << a55 << ", 220 Hz " << a220 << std::endl;
        check (a55 < 0.05, "sub ducked");
        check (a220 > 0.17, "top passes");
    }

    std::cout << "\n[5] Grid mode with a dropped kick sample (no sidechain)" << std::endl;
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("undertow_test_kick.wav");
        {
            f.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 44100, 1, 24, {}, 0));
            juce::AudioBuffer<float> b (1, (int) ks.size());
            for (size_t i = 0; i < ks.size(); ++i) b.setSample (0, (int) i, ks[i]);
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        Rig r (false);
        r.p->setCurve (gate);
        check (r.p->loadKickFile (f), "kick sample loads");
        auto out = r.run (bass, std::vector<float> ((size_t) N, 0.0f));
        double atBeat = 0, closed = 0;
        for (int b = 2; b < 10; ++b)
        {
            const int t = b * beat + r.D;
            atBeat = juce::jmax (atBeat, (double) std::abs (out[(size_t) t]));
            closed += rms (out, t + 10, t + beat / 4 - 50) / 8;
        }
        std::cout << "  |out| on the beat " << atBeat << ", closed rms " << closed << std::endl;
        check (atBeat < 0.02 && closed < 0.01, "ducks on the host grid");
        check (r.p->display.status.load() == DisplayData::sample, "status shows sample");
        check (r.p->display.kickRef.load(), "kick reference available for phase/space");
    }

    std::cout << "\n[6] Hold: the kick stops, the duck carries on" << std::endl;
    {
        Rig r (true);
        r.p->setCurve (gate);
        auto k2 = kick;
        for (int i = (int) (FS * 3); i < N; ++i) k2[(size_t) i] = 0;   // kick stops after 3 s
        std::vector<float> longBass = bass, longKick = k2;
        longBass.insert (longBass.end(), bass.begin(), bass.end());
        longKick.resize (longBass.size(), 0.0f);
        auto out = r.run (longBass, longKick);
        // kicks would have continued at kickOffset + k*beat
        double closed = 0; int c = 0;
        for (int t = kickOffset + beat * 14; t + beat < (int) longBass.size() - r.D; t += beat, ++c)
            closed += rms (out, t + r.D + 100, t + r.D + beat / 4 - 100);
        std::cout << "  closed rms after the kick stopped " << closed / c << std::endl;
        check (closed / c < 0.02, "duck keeps the last kick's timing");
        check (r.p->display.status.load() == DisplayData::hold, "status shows hold");
    }

    std::cout << "\n[7] Phase AUTO turns the sub towards the kick tail" << std::endl;
    {
        // a bass at the kick's tail frequency but in anti-phase with it
        std::vector<float> b2 ((size_t) N);
        for (int i = 0; i < N; ++i) b2[(size_t) i] = (float) (0.4 * std::sin (juce::MathConstants<double>::twoPi * 48 * i / FS + 2.4));
        auto fast = Shapes::builtIn()[2].curve;
        double corr[2];
        float angleDeg = 0;
        for (int on = 0; on < 2; ++on)
        {
            Rig r (true);
            r.p->setCurve (fast);
            r.set (ID::duck, 1);
            r.set (ID::phaseOn, (float) on);
            auto out = r.run (b2, kick);
            double c = 0, e1 = 0, e2 = 0;
            for (size_t k = 6; k + 1 < kickTimes.size(); ++k)
                for (int i = (int) (0.02 * FS); i < (int) (0.2 * FS); ++i)
                {
                    const int t = kickTimes[k] + i;
                    const double kv = kick[(size_t) t], ov = out[(size_t) t + r.D];
                    c += kv * ov; e1 += kv * kv; e2 += ov * ov;
                }
            corr[on] = c / std::sqrt (e1 * e2 + 1e-12);
            if (on) angleDeg = r.p->display.angleDeg.load();
        }
        std::cout << "  kick/bass correlation in the overlap: off " << corr[0] << ", auto " << corr[1] << ", rotation " << angleDeg << " deg" << std::endl;
        check (corr[1] > corr[0] + 0.2 && corr[1] > 0.3, "auto phase makes kick tail and sub add up");
    }

    std::cout << "\n[8] Space cuts the bass where the kick lives, only during the kick" << std::endl;
    {
        std::vector<float> b3 ((size_t) N);
        for (int i = 0; i < N; ++i) b3[(size_t) i] = (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 50 * i / FS)
                                                          + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 180 * i / FS));
        double a50[2], a180[2], late50[2];
        for (int on = 0; on < 2; ++on)
        {
            Rig r (true);
            r.set (ID::depth, 0);
            r.set (ID::spaceOn, (float) on);
            r.set (ID::spaceAmount, 100);
            auto out = r.run (b3, kick);
            const int t = kickTimes[7] + r.D;
            a50[on]  = toneAmp (out, t + 1500, t + 6300, 50);
            a180[on] = toneAmp (out, t + 7200, t + 12000, 180);
            late50[on] = toneAmp (out, t + 21000, t + 23900, 50);
        }
        std::cout << "  during kick 50 Hz: " << a50[0] << " -> " << a50[1] << ", 180 Hz: " << a180[0] << " -> " << a180[1]
                  << ", 50 Hz before next kick: " << late50[0] << " -> " << late50[1] << std::endl;
        check (a50[1] < 0.6 * a50[0], "cut in the kick's band");
        check (a180[1] > 0.85 * a180[0], "bands without kick energy untouched");
        check (late50[1] > 0.85 * late50[0], "cut released after the kick");
    }

    std::cout << "\n[9] State round trip" << std::endl;
    {
        Rig a (true);
        a.set (ID::depth, 63); a.set (ID::duck, 2); a.set (ID::phaseOn, 1);
        a.p->setCurve (Shapes::builtIn()[10].curve);
        juce::MemoryBlock mb;
        a.p->getStateInformation (mb);
        Rig b (true);
        b.p->setStateInformation (mb.getData(), (int) mb.getSize());
        check (a.p->snapshot() == b.p->snapshot(), "parameters and curve restored");
    }

    std::cout << "\n[10] CPU" << std::endl;
    for (int cfg = 0; cfg < 4; ++cfg)
    {
        Rig r (true);
        if (cfg >= 1) r.set (ID::duck, 2);
        if (cfg >= 2) r.set (ID::phaseOn, 1);
        if (cfg >= 3) r.set (ID::spaceOn, 1);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        r.run (bass, kick);
        std::cout << "  config " << cfg << ": " << (6000.0 / (juce::Time::getMillisecondCounterHiRes() - t0)) << "x realtime" << std::endl;
    }
    {
        Rig r (true);
        r.set (ID::duck, 2); r.set (ID::phaseOn, 1); r.set (ID::spaceOn, 1);
        std::vector<float> lb, lk;
        for (int k = 0; k < 10; ++k) { lb.insert (lb.end(), bass.begin(), bass.end()); lk.insert (lk.end(), kick.begin(), kick.end()); }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        r.run (lb, lk);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  60 s of stereo audio with every feature on: " << ms << " ms (" << (60000.0 / ms) << "x realtime)" << std::endl;
        check (60000.0 / ms > 20, "fast enough");
    }

    std::cout << "\n[11] Editor screenshots" << std::endl;
    {
        auto outDir = juce::File ("/home/claude/undertow/shots");
        outDir.createDirectory();
        Rig r (true);
        r.p->setCurve (Shapes::builtIn()[0].curve);
        r.set (ID::phaseOn, 1); r.set (ID::spaceOn, 1); r.set (ID::spaceAmount, 60);
        r.run (bass, kick);
        for (int tab = 0; tab < 3; ++tab)
        {
            r.p->uiState.setProperty ("tab", tab, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (r.p->createEditor());
            ed->setSize (1200, 720);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            auto f = outDir.getChildFile ("tab" + juce::String (tab) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            png.writeImageToStream (img, os);
        }
        check (outDir.getChildFile ("tab2.png").existsAsFile(), "screenshots written");
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
