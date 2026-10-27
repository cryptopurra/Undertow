#pragma once
#include <JuceHeader.h>
#include "Curve.h"
#include "LinearPhaseBank.h"
#include "SpaceEQ.h"
#include "Presets.h"

namespace ID
{
    inline const juce::String rate = "rate", depth = "depth", click = "click", duck = "duck",
        bandLo = "bandLo", bandHi = "bandHi", gridOn = "gridOn", bypass = "bypass",
        phaseOn = "phaseOn", phaseMode = "phaseMode", phaseAngle = "phaseAngle", split = "split", flip = "flip",
        spaceOn = "spaceOn", spaceAmount = "spaceAmount", spaceRelease = "spaceRelease",
        spaceLo = "spaceLo", spaceHi = "spaceHi";
}

// a kick sample at the current sample rate
struct KickSample : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<KickSample>;
    juce::AudioBuffer<float> data;     // mono, at playback rate
    juce::String name;
};

struct DisplayData
{
    static constexpr int cycleBins = 256, phaseBins = 300;
    template <size_t N> using AF = std::array<std::atomic<float>, N>;
    AF<cycleBins> kick, bass, out, spaceCut;
    AF<phaseBins> phKick, phBass;
    AF<SpaceEQ::numBands> kEnv, bEnv, gainDb, bandFreq, bandQ;
    std::atomic<float> playhead { 0 }, angleDeg { 0 }, targetDeg { 0 }, coherence { 0 },
                       periodMs { 500 }, bpm { 120 }, phaseWindowMs { 150 };
    std::atomic<int> status { 0 };
    std::atomic<bool> kickRef { false };
    std::atomic<bool> phaseView { false };
    std::atomic<bool> spaceView { false };   // editor shows the phase page: keep its analysis running

    enum Status { gridNoKick = 0, routed, sample, hold, gridOff };

    DisplayData()
    {
        for (auto* a : { &kick, &bass, &out, &spaceCut }) for (auto& v : *a) v = 0;
        for (auto& v : phKick) v = 0;
        for (auto& v : phBass) v = 0;
        for (auto* a : { &kEnv, &bEnv, &gainDb, &bandFreq, &bandQ }) for (auto& v : *a) v = 0;
    }
};

class UndertowProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    UndertowProcessor();
    ~UndertowProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (ID::bypass); }

    // ---- curve (message thread) ----
    Curve getCurve() const;
    void setCurve (const Curve&);

    // ---- kick sample (message thread) ----
    bool loadKickFile (const juce::File&);
    void clearKick();
    juce::String getKickName() const;

    // ---- presets / shapes ----
    void applyPreset (int index);
    static juce::File getUserDir();

    // full snapshot for undo
    juce::String snapshot();
    void restoreSnapshot (const juce::String&);

    juce::AudioProcessorValueTreeState apvts;
    DisplayData display;
    juce::ValueTree uiState { "UI" };    // editor-only settings (tab, lanes, grid, quantize)
    int currentPreset = 1;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static float rateBeats (int idx, int tsNum, int tsDen);

private:
    void rebuildKickForRate();
    juce::ValueTree stateWithExtras();
    void restoreFromTree (const juce::ValueTree&);

    // message-thread copies
    Curve curve;
    mutable juce::CriticalSection curveLock;
    juce::AudioBuffer<float> kickOriginal;
    double kickOriginalRate = 44100;
    juce::String kickName;

    // curve table handed to the audio thread
    static constexpr int tableSize = 4096;
    juce::SpinLock tableLock;
    std::vector<float> pendingTable = std::vector<float> (tableSize, 1.0f);
    std::atomic<bool> tableDirty { true };
    std::vector<float> table = std::vector<float> (tableSize, 1.0f);

    // kick sample handover
    juce::SpinLock kickLock;
    KickSample::Ptr pendingKick, kick;
    std::atomic<bool> kickDirty { false };
    juce::ReferenceCountedArray<KickSample> kickGarbage;

    // ---- audio thread ----
    double fs = 48000;
    LinearPhaseBank bank;
    SpaceEQ space;
    int D = 0;
    juce::int64 n = 0;

    // delay lines
    std::vector<float> dlX[2], dlK;
    int dlMask = 0;
    juce::int64 dlPos = 0;

    // sidechain history / detection
    std::vector<float> scHist;
    int scMask = 0;
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void lowpass (double fs, double f, double q)
        {
            const double w = juce::MathConstants<double>::twoPi * f / fs, cw = std::cos (w), al = std::sin (w) / (2 * q);
            const double a0 = 1 + al;
            b0 = (float) ((1 - cw) / 2 / a0); b1 = (float) ((1 - cw) / a0); b2 = b0;
            a1 = (float) (-2 * cw / a0); a2 = (float) ((1 - al) / a0); z1 = z2 = 0;
        }
        inline float tick (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    } detLp1, detLp2;
    float detFast = 0, detSlow = 0, scLevel = 0;
    bool detArmed = true;
    juce::int64 lastScKick = -(1LL << 40);
    juce::int64 captureAt = -1, captureStart = 0;
    std::vector<float> capture;
    int captureLen = 0;
    bool hasCapture = false;

    // events (input time)
    static constexpr int evSize = 64;
    std::array<juce::int64, evSize> ev {};
    juce::int64 evW = 0, evCur = -1;   // evCur indexes the last event <= m
    juce::int64 lastEvent = -(1LL << 40);

    // grid
    double internalPpq = 0, lastPpqEnd = -1, holdOffsetBeats = 0;
    bool haveHoldOffset = false;
    double prevGridIdx = -1e18;

    // gain
    float curveSm = 1, depthSm = 1, bypassSm = 0;
    // phase
    float angle = 0, cosA = 1, sinA = 0, trigAngle = 0;
    double accC = 0, accS = 0, accK = 0, accB = 0, vecC = 0, vecS = 0;
    float targetAngle = 0;
    // display accumulators
    std::array<float, DisplayData::cycleBins> cyK {}, cyB {}, cyO {}, cyS {};
    std::array<float, DisplayData::phaseBins> phK {}, phB {};
    juce::int64 curEventTime = -1;

    void timerCallback() override;
    void pushEvent (juce::int64 t);
    float tableLookup (float x) const;
    void finishCycle();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowProcessor)
};
