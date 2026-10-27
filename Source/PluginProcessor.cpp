#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr float subHi = 120.0f;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout UndertowProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto hz = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };
    auto hzAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 1000 ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; }).withLabel ("Hz");
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; });
    auto ms  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10 ? 1 : 0) + " ms"; });
    auto deg = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0"); });

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::rate, 1 }, "Rate", StringArray { "1/16", "1/8", "1/4", "1/2", "1 Bar" }, 2));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::depth, 1 }, "Depth", NormalisableRange<float> (0, 100, 0.1f), 100.0f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::click, 1 }, "Click", NormalisableRange<float> (0, 10, 0.01f, 0.6f), 2.0f, ms));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::duck, 1 }, "Duck", StringArray { "Full", "Sub", "Band" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::bandLo, 1 }, "Band Low",  hz (20, 2000, 120), 20.0f, hzAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::bandHi, 1 }, "Band High", hz (40, 20000, 600), 200.0f, hzAttr));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { ID::gridOn, 1 }, "Grid", true));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { ID::bypass, 1 }, "Bypass", false));

    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { ID::phaseOn, 1 }, "Phase", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ID::phaseMode, 1 }, "Phase Mode", StringArray { "Auto", "Manual" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::phaseAngle, 1 }, "Phase Angle", NormalisableRange<float> (-180, 180, 0.1f), 0.0f, deg));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::split, 1 }, "Split", hz (40, 200, 90), 90.0f, hzAttr));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { ID::flip, 1 }, "Flip", false));

    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { ID::spaceOn, 1 }, "Space", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::spaceAmount, 1 }, "Space Amount", NormalisableRange<float> (0, 100, 0.1f), 30.0f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::spaceRelease, 1 }, "Space Release", NormalisableRange<float> (5, 500, 0.1f, 0.4f), 30.0f, ms));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::spaceLo, 1 }, "Space Low",  hz (20, 500, 60), 20.0f, hzAttr));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::spaceHi, 1 }, "Space High", hz (60, 1000, 250), 250.0f, hzAttr));
    return { p.begin(), p.end() };
}

float UndertowProcessor::rateBeats (int idx, int tsNum, int tsDen)
{
    switch (idx)
    {
        case 0: return 0.25f;
        case 1: return 0.5f;
        case 2: return 1.0f;
        case 3: return 2.0f;
        default: return (float) tsNum * 4.0f / (float) juce::jmax (1, tsDen);
    }
}

UndertowProcessor::UndertowProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "UNDERTOW", createLayout())
{
    uiState.setProperty ("tab", 0, nullptr);
    uiState.setProperty ("overlay", true, nullptr);
    uiState.setProperty ("grid", true, nullptr);
    uiState.setProperty ("quantize", false, nullptr);
    setCurve (Shapes::builtIn()[0].curve);
    startTimer (1000);
}

bool UndertowProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& mo = l.getMainOutputChannelSet();
    if (mo != juce::AudioChannelSet::mono() && mo != juce::AudioChannelSet::stereo()) return false;
    if (l.getMainInputChannelSet() != mo) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto& sc = l.inputBuses[1];
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

//==============================================================================
void UndertowProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    const float split = apvts.getRawParameterValue (ID::split)->load();
    bank.setEdges (split, -1, 200);
    bank.prepare (fs);
    D = bank.getLatency();
    setLatencySamples (D);
    space.prepare (fs);

    int sz = 1;
    while (sz < D + 8) sz <<= 1;
    dlMask = sz - 1;
    for (auto& d : dlX) d.assign ((size_t) sz, 0.0f);
    dlK.assign ((size_t) sz, 0.0f);
    dlPos = 0;

    int hs = 1;
    while (hs < (int) (0.6 * fs)) hs <<= 1;
    scMask = hs - 1;
    scHist.assign ((size_t) hs, 0.0f);
    captureLen = (int) (0.35 * fs);
    capture.assign ((size_t) captureLen, 0.0f);
    hasCapture = false;
    captureAt = -1;

    detLp1.lowpass (fs, 150, 0.5412);
    detLp2.lowpass (fs, 150, 1.3066);
    detFast = detSlow = scLevel = 0;
    detArmed = true;

    n = 0;
    evW = 0; evCur = -1;
    lastEvent = lastScKick = -(1LL << 40);
    curEventTime = -1;
    prevGridIdx = -1e18;
    lastPpqEnd = -1;
    curveSm = 1; depthSm = apvts.getRawParameterValue (ID::depth)->load() * 0.01f;
    bypassSm = apvts.getRawParameterValue (ID::bypass)->load() > 0.5f ? 1.0f : 0.0f;
    angle = 0; cosA = 1; sinA = 0; trigAngle = 0; accC = accS = accK = accB = 0; vecC = 1; vecS = 0; targetAngle = 0;
    cyK.fill (0); cyB.fill (0); cyO.fill (0); cyS.fill (0); phK.fill (0); phB.fill (0);

    rebuildKickForRate();
    {
        const juce::SpinLock::ScopedLockType l (kickLock);
        kick = pendingKick;
        kickDirty = false;
    }
    {
        const juce::SpinLock::ScopedLockType l (tableLock);
        table = pendingTable;
        tableDirty = false;
    }
}

void UndertowProcessor::pushEvent (juce::int64 t)
{
    if (t <= lastEvent) t = lastEvent + 1;
    ev[(size_t) (evW % evSize)] = t;
    ++evW;
    lastEvent = t;
}

float UndertowProcessor::tableLookup (float x) const
{
    const float p = juce::jlimit (0.0f, 1.0f, x) * (tableSize - 1);
    const int i = juce::jmin ((int) p, tableSize - 2);
    const float f = p - (float) i;
    return table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
}

void UndertowProcessor::finishCycle()
{
    // publish the cycle that just ended
    for (int i = 0; i < DisplayData::cycleBins; ++i)
    {
        display.kick[(size_t) i].store (cyK[(size_t) i], std::memory_order_relaxed);
        display.bass[(size_t) i].store (cyB[(size_t) i], std::memory_order_relaxed);
        display.out[(size_t) i].store (cyO[(size_t) i], std::memory_order_relaxed);
        display.spaceCut[(size_t) i].store (cyS[(size_t) i], std::memory_order_relaxed);
    }
    for (int i = 0; i < DisplayData::phaseBins; ++i)
    {
        display.phKick[(size_t) i].store (phK[(size_t) i], std::memory_order_relaxed);
        display.phBass[(size_t) i].store (phB[(size_t) i], std::memory_order_relaxed);
    }
    cyK.fill (0); cyB.fill (0); cyO.fill (0); cyS.fill (0); phK.fill (0); phB.fill (0);

    // phase measurement over the cycle: angle that makes the bass sub best continue the kick
    if (accK > 1.0e-7 && accB > 1.0e-7)
    {
        const double norm = std::sqrt (accC * accC + accS * accS);
        const double coh = norm / std::sqrt (accK * accB);
        display.coherence.store ((float) coh);
        if (coh > 0.05 && norm > 0)
        {
            vecC = 0.6 * vecC + 0.4 * accC / norm;
            vecS = 0.6 * vecS + 0.4 * accS / norm;
            targetAngle = (float) std::atan2 (vecS, vecC);
        }
    }
    accC = accS = accK = accB = 0;
}

void UndertowProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int ns = buffer.getNumSamples();
    auto mainOut = getBusBuffer (buffer, false, 0);
    const int numCh = juce::jmin (2, mainOut.getNumChannels());
    if (numCh == 0 || ns == 0) return;

    const float* sc[2] = { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0)
        {
            auto scb = getBusBuffer (buffer, true, 1);
            scCh = juce::jmin (2, scb.getNumChannels());
            for (int c = 0; c < scCh; ++c) sc[c] = scb.getReadPointer (c);
        }

    // hand-overs from the message thread
    if (tableDirty.load())
    {
        const juce::SpinLock::ScopedTryLockType l (tableLock);
        if (l.isLocked()) { std::copy (pendingTable.begin(), pendingTable.end(), table.begin()); tableDirty = false; }
    }
    if (kickDirty.load())
    {
        const juce::SpinLock::ScopedTryLockType l (kickLock);
        if (l.isLocked()) { kick = pendingKick; kickDirty = false; }
    }

    // parameters
    auto P = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    const int rateIdx   = (int) P (ID::rate);
    const float depth   = P (ID::depth) * 0.01f;
    const float clickMs = P (ID::click);
    const int duck      = (int) P (ID::duck);
    const float bandLo  = P (ID::bandLo);
    const float bandHi  = juce::jmax (P (ID::bandHi), bandLo * 1.2f);
    const bool gridOn   = P (ID::gridOn) > 0.5f;
    const bool bypass   = P (ID::bypass) > 0.5f;
    const bool phaseOn  = P (ID::phaseOn) > 0.5f;
    const bool phaseAuto = (int) P (ID::phaseMode) == 0;
    const float manualAngle = P (ID::phaseAngle) * juce::MathConstants<float>::pi / 180.0f;
    const float split   = P (ID::split);
    const bool flip     = P (ID::flip) > 0.5f;
    const bool spaceOn  = P (ID::spaceOn) > 0.5f;
    space.setParams (spaceOn, P (ID::spaceAmount) * 0.01f, P (ID::spaceRelease));
    space.setRange (P (ID::spaceLo), P (ID::spaceHi));

    // edges for the linear-phase bank
    const bool loOn = duck == 2 && bandLo > 25.0f;
    const bool hiOn = duck == 1 || bandHi < 0.45f * (float) fs;
    const float loEdge = loOn ? bandLo : -1.0f;
    const float hiEdge = duck == 1 ? subHi : (hiOn ? bandHi : -1.0f);
    bank.setEdges (split, duck == 0 ? (bandLo > 25.0f ? bandLo : -1.0f) : loEdge, hiEdge);
    {
        using LB = LinearPhaseBank;
        const bool phaseNeeded = phaseOn || std::abs (angle) > 1.0e-5f || display.phaseView.load();
        juce::uint32 mask = 0;
        auto setBit = [&mask] (int slotIndex) { mask |= (1u << slotIndex); };
        for (int c = 0; c < 2; ++c)
        {
            if (phaseNeeded) { setBit (LB::slot (c, LB::kLs)); setBit (LB::slot (c, LB::kHLs)); }
            if (duck != 0)   { setBit (LB::slot (c, LB::kLhi)); if (phaseNeeded) setBit (LB::slot (c, LB::kHLhi)); }
            if (duck == 2 && loOn) { setBit (LB::slot (c, LB::kLlo)); if (phaseNeeded) setBit (LB::slot (c, LB::kHLlo)); }
        }
        if (phaseNeeded) setBit (LB::slot (2, LB::kLs));
        bank.setActiveSlots (mask);
    }

    // transport
    double bpm = 120.0, ppq0 = internalPpq;
    int tsN = 4, tsD = 4;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ts = pos->getTimeSignature()) { tsN = ts->numerator; tsD = ts->denominator; }
            playing = pos->getIsPlaying();
            if (playing) if (auto pq = pos->getPpqPosition()) ppq0 = *pq;
        }
    bpm = juce::jlimit (20.0, 999.0, bpm);
    const double bps = bpm / 60.0 / fs;
    const double rb = rateBeats (rateIdx, tsN, tsD);
    const double period = rb / bps;                 // samples per curve cycle
    const double beatSamples = 1.0 / bps;
    const double gridOffset = haveHoldOffset ? holdOffsetBeats : 0.0;
    if (std::abs (ppq0 - lastPpqEnd) > 4 * bps)
        prevGridIdx = std::floor ((ppq0 - gridOffset) / rb - 1.0e-9);
    lastPpqEnd = ppq0 + ns * bps;
    internalPpq = lastPpqEnd;
    display.periodMs.store ((float) (period / fs * 1000.0));
    display.bpm.store ((float) bpm);

    const float clickSamples = clickMs * 0.001f * (float) fs;
    const float slew = 1.0f - std::exp (-1.0f / (0.0007f * (float) fs));
    const float depthK = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
    const float scRel = std::exp (-1.0f / (1.0f * (float) fs));
    const float fA = 1.0f - std::exp (-1.0f / (0.0003f * (float) fs)), fR = 1.0f - std::exp (-1.0f / (0.010f * (float) fs));
    const float sA = 1.0f - std::exp (-1.0f / (0.025f * (float) fs)), sR = 1.0f - std::exp (-1.0f / (0.150f * (float) fs));
    const double refractory = juce::jmax (0.06 * fs, 0.3 * juce::jmin (period, beatSamples));
    const double holdWindow = juce::jmax (0.75 * fs, 2.2 * juce::jmax (period, beatSamples));
    const float manualStep = juce::MathConstants<float>::pi / (0.03f * (float) fs);
    const float autoStep   = juce::MathConstants<float>::pi / (0.004f * (float) fs);
    const float pi = juce::MathConstants<float>::pi;
    const int phaseBinLen = juce::jmax (1, (int) (display.phaseWindowMs.load() * 0.001 * fs / DisplayData::phaseBins));

    const bool spaceRun = spaceOn || ! space.isIdle() || display.spaceView.load();
    KickSample* ks = kick.get();
    const float* kickSrc = ks != nullptr ? ks->data.getReadPointer (0) : (hasCapture ? capture.data() : nullptr);
    const juce::int64 kickLen = ks != nullptr ? ks->data.getNumSamples() : (hasCapture ? captureLen : 0);

    float* outPtr[2] = { mainOut.getWritePointer (0), numCh > 1 ? mainOut.getWritePointer (1) : nullptr };
    float bankIn[3], o[LinearPhaseBank::numSlots];
    float playhead = -1;

    for (int i = 0; i < ns; ++i, ++n)
    {
        const float xL = outPtr[0][i];
        const float xR = numCh > 1 ? outPtr[1][i] : xL;

        // ---------------- sidechain & triggers (input time n) ----------------
        float s = 0;
        if (scCh == 1) s = sc[0][i];
        else if (scCh == 2) s = 0.5f * (sc[0][i] + sc[1][i]);
        scHist[(size_t) (n & scMask)] = s;
        scLevel = juce::jmax (std::abs (s), scLevel * scRel);
        const bool scPresent = scCh > 0 && scLevel > 1.0e-4f;

        if (scCh > 0)
        {
            const float a = std::abs (detLp2.tick (detLp1.tick (s)));
            detFast += (a > detFast ? fA : fR) * (a - detFast);
            detSlow += (a > detSlow ? sA : sR) * (a - detSlow);
            if (! detArmed && (double) (n - lastScKick) > refractory && detFast < 1.6f * detSlow) detArmed = true;
            if (detArmed && detFast > 0.008f && detFast > 2.0f * detSlow + 0.001f)
            {
                const int W = (int) (0.008 * fs);
                float pk = 0;
                for (int k = 0; k <= W; ++k) pk = juce::jmax (pk, std::abs (scHist[(size_t) ((n - k) & scMask)]));
                juce::int64 e = n;
                for (int k = W; k >= 0; --k)
                    if (std::abs (scHist[(size_t) ((n - k) & scMask)]) >= 0.1f * pk) { e = n - k; break; }
                detArmed = false;
                if ((double) (e - lastScKick) > refractory)
                {
                    // a grid event a few ms earlier is close enough; otherwise this kick starts the cycle
                    if (e > lastEvent) pushEvent (e);
                    lastScKick = e;
                    const double ppqE = ppq0 + (i - (n - e)) * bps;
                    holdOffsetBeats = ppqE - rb * std::floor (ppqE / rb);
                    haveHoldOffset = true;
                    prevGridIdx = std::floor ((ppqE - holdOffsetBeats) / rb + 1.0e-6);
                    captureStart = e;
                    captureAt = e + captureLen;
                }
            }
            if (n == captureAt)
            {
                for (int k = 0; k < captureLen; ++k)
                    capture[(size_t) k] = scHist[(size_t) ((captureStart + k) & scMask)];
                hasCapture = true;
                captureAt = -1;
            }
        }

        const bool scKicking = (double) (n - lastScKick) < holdWindow;
        {
            const double ppq = ppq0 + i * bps;
            const double gi = std::floor ((ppq - (haveHoldOffset ? holdOffsetBeats : 0.0)) / rb);
            if (gi > prevGridIdx)
            {
                if (! scKicking && gridOn && (double) (n - lastEvent) > 0.25 * period)
                    pushEvent (n);
                prevGridIdx = gi;
            }
        }

        // kick reference
        float kr = 0;
        if (scPresent) kr = s;
        else if (kickSrc != nullptr)
        {
            const juce::int64 idx = n - lastEvent;
            if (idx >= 0 && idx < kickLen) kr = kickSrc[idx];
        }

        // ---------------- linear-phase bank and delays ----------------
        bankIn[0] = xL; bankIn[1] = xR; bankIn[2] = kr;
        bank.process (bankIn, o);

        const auto w = (size_t) (dlPos & dlMask), r = (size_t) ((dlPos - D) & dlMask);
        dlX[0][w] = xL; dlX[1][w] = xR; dlK[w] = kr;
        const float xd[2] = { dlX[0][r], dlX[1][r] };
        const float kd = dlK[r];
        ++dlPos;

        // ---------------- gain at output time m ----------------
        const juce::int64 m = n - D;
        if (evW - evCur > evSize - 2) evCur = evW - evSize + 2;
        bool newCycle = false;
        while (evCur + 1 < evW && ev[(size_t) ((evCur + 1) % evSize)] <= m) { ++evCur; newCycle = true; }
        if (newCycle) { finishCycle(); curEventTime = ev[(size_t) (evCur % evSize)]; }

        float cv = 1.0f;
        double xPos = -1;
        if (curEventTime >= 0)
        {
            xPos = (double) (m - curEventTime) / period;
            cv = xPos >= 1.0 ? table.back() : tableLookup ((float) xPos);
            if (m == curEventTime) curveSm = cv;
        }
        curveSm += slew * (cv - curveSm);
        float fade = 1.0f;
        if (clickSamples > 0.5f && evCur + 1 < evW)
        {
            const float d = (float) (ev[(size_t) ((evCur + 1) % evSize)] - m);
            if (d < clickSamples) { const float sn = std::sin (0.5f * pi * d / clickSamples); fade = sn * sn; }
        }
        depthSm += depthK * (depth - depthSm);
        const float g = 1.0f - depthSm * (1.0f - curveSm * fade);

        // ---------------- phase ----------------
        const float tgt = phaseOn ? (phaseAuto ? targetAngle : manualAngle) + (flip ? pi : 0.0f) : 0.0f;
        if (tgt != 0.0f || angle != 0.0f)
        {
            const bool closed = g < (1.0f - depthSm) + 0.1f;
            const float step = (phaseOn && phaseAuto) ? (closed ? autoStep : 0.0f) : manualStep;
            const float diff = std::remainder (tgt - angle, 2.0f * pi);
            angle = std::abs (diff) <= step && tgt == 0.0f ? 0.0f : std::remainder (angle + juce::jlimit (-step, step, diff), 2.0f * pi);
            if (angle != trigAngle) { trigAngle = angle; cosA = std::cos (angle); sinA = std::sin (angle); }
        }
        const bool rotating = angle != 0.0f;
        const float ca = cosA, sa = sinA;

        float outS[2], subHeard = 0, bsM = 0, bhM = 0;
        for (int c = 0; c < 2; ++c)
        {
            using LB = LinearPhaseBank;
            const float Ls = o[LB::slot (c, LB::kLs)], HLs = o[LB::slot (c, LB::kHLs)];
            const float rotS = rotating ? ca * Ls + sa * HLs : Ls;
            const float y = xd[c] + (rotS - Ls);
            auto edgeY = [&] (float e, int kl)
            {
                const float L = o[LB::slot (c, kl)], HL = o[LB::slot (c, kl + 1)];
                if (! rotating) return L;
                return e <= split ? ca * L + sa * HL : L - Ls + rotS;
            };
            float By;
            if (duck == 0) By = y;
            else if (duck == 1) By = edgeY (subHi, LB::kLhi);
            else By = (hiOn ? edgeY (bandHi, LB::kLhi) : y) - (loOn ? edgeY (bandLo, LB::kLlo) : 0.0f);
            outS[c] = y + (g - 1.0f) * By;
            subHeard += 0.5f * rotS;
            bsM += 0.5f * Ls; bhM += 0.5f * HLs;
        }

        // phase measurement (unrotated bass sub vs kick sub, weighted by how much bass is heard)
        {
            const float kLs = o[LinearPhaseBank::slot (2, LinearPhaseBank::kLs)];
            accC += (double) (kLs * bsM * g);
            accS += (double) (kLs * bhM * g);
            accK += (double) (kLs * kLs * g);
            accB += (double) (bsM * bsM * g);
            if (curEventTime >= 0)
            {
                const juce::int64 t = m - curEventTime;
                if (t % phaseBinLen == 0 && t / phaseBinLen < DisplayData::phaseBins)
                {
                    const bool subDucked = duck != 2 || bandLo < split;
                    phK[(size_t) (t / phaseBinLen)] = kLs;
                    phB[(size_t) (t / phaseBinLen)] = subHeard * (subDucked ? g : 1.0f);
                }
            }
        }

        // ---------------- space ----------------
        float cut = 0;
        if (spaceRun)
        {
            const float preMono = 0.5f * (outS[0] + outS[1]);
            cut = space.process (kd, preMono, outS, numCh);
        }

        // ---------------- display ----------------
        if (xPos >= 0 && xPos < 1.0)
        {
            const auto b = (size_t) (xPos * DisplayData::cycleBins);
            cyK[b] = juce::jmax (cyK[b], std::abs (kd));
            cyB[b] = juce::jmax (cyB[b], std::abs (0.5f * (xd[0] + xd[1])));
            cyO[b] = juce::jmax (cyO[b], std::abs (0.5f * (outS[0] + outS[1])));
            cyS[b] = juce::jmin (cyS[b], cut);
            playhead = (float) xPos;
        }

        // ---------------- bypass & write ----------------
        bypassSm += depthK * ((bypass ? 1.0f : 0.0f) - bypassSm);
        for (int c = 0; c < numCh; ++c)
            outPtr[c][i] = bypassSm * xd[c] + (1.0f - bypassSm) * outS[c];
    }

    // publish
    display.playhead.store (playhead);
    display.angleDeg.store (angle * 180.0f / pi);
    display.targetDeg.store (targetAngle * 180.0f / pi);
    const bool scKickingNow = (double) (n - lastScKick) < holdWindow;
    int st = DisplayData::gridNoKick;
    if (scKickingNow) st = DisplayData::routed;
    else if (! gridOn) st = DisplayData::gridOff;
    else if (ks != nullptr) st = DisplayData::sample;
    else if (hasCapture) st = DisplayData::hold;
    display.status.store (st);
    display.kickRef.store (scLevel > 1.0e-4f || ks != nullptr || hasCapture);
    for (int b = 0; b < SpaceEQ::numBands; ++b)
    {
        display.kEnv[(size_t) b].store (space.kEnv[b]);
        display.bEnv[(size_t) b].store (space.bEnv[b]);
        display.gainDb[(size_t) b].store (space.gainDb[b]);
        display.bandFreq[(size_t) b].store (space.freq[b]);
        display.bandQ[(size_t) b].store (space.qs[b]);
    }

    for (int c = numCh; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, ns);
}

//==============================================================================
Curve UndertowProcessor::getCurve() const
{
    const juce::ScopedLock l (curveLock);
    return curve;
}

void UndertowProcessor::setCurve (const Curve& c)
{
    Curve cc = c;
    cc.normalise();
    {
        const juce::ScopedLock l (curveLock);
        curve = cc;
    }
    std::vector<float> t ((size_t) tableSize);
    cc.fillTable (t.data(), tableSize);
    {
        const juce::SpinLock::ScopedLockType l (tableLock);
        pendingTable = std::move (t);
        tableDirty = true;
    }
}

//==============================================================================
bool UndertowProcessor::loadKickFile (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) return false;

    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 2.0));
    if (len <= 0) return false;
    juce::AudioBuffer<float> tmp ((int) reader->numChannels, len);
    reader->read (&tmp, 0, len, 0, true, true);

    juce::AudioBuffer<float> mono (1, len);
    mono.clear();
    for (int c = 0; c < tmp.getNumChannels(); ++c) mono.addFrom (0, 0, tmp, c, 0, len, 1.0f / (float) tmp.getNumChannels());

    // the kick starts at its first audible sample
    const float pk = mono.getMagnitude (0, 0, len);
    if (pk <= 0) return false;
    int start = 0;
    while (start < len - 1 && std::abs (mono.getSample (0, start)) < 0.01f * pk) ++start;
    juce::AudioBuffer<float> trimmed (1, len - start);
    trimmed.copyFrom (0, 0, mono, 0, start, len - start);

    kickOriginal = std::move (trimmed);
    kickOriginalRate = reader->sampleRate;
    kickName = f.getFileNameWithoutExtension();
    rebuildKickForRate();
    return true;
}

void UndertowProcessor::clearKick()
{
    kickOriginal.setSize (0, 0);
    kickName = {};
    const juce::SpinLock::ScopedLockType l (kickLock);
    pendingKick = nullptr;
    kickDirty = true;
}

juce::String UndertowProcessor::getKickName() const { return kickName; }

void UndertowProcessor::rebuildKickForRate()
{
    KickSample::Ptr ks;
    if (kickOriginal.getNumSamples() > 0)
    {
        ks = new KickSample();
        ks->name = kickName;
        const double ratio = kickOriginalRate / fs;
        const int outLen = (int) std::ceil (kickOriginal.getNumSamples() / ratio);
        ks->data.setSize (1, outLen);
        ks->data.clear();
        juce::LagrangeInterpolator interp;
        interp.process (ratio, kickOriginal.getReadPointer (0), ks->data.getWritePointer (0), outLen,
                        kickOriginal.getNumSamples(), 0);
        kickGarbage.add (ks);
    }
    const juce::SpinLock::ScopedLockType l (kickLock);
    pendingKick = ks;
    kickDirty = true;
}

void UndertowProcessor::timerCallback()
{
    for (int i = kickGarbage.size(); --i >= 0;)
        if (kickGarbage.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            kickGarbage.remove (i);
}

//==============================================================================
juce::File UndertowProcessor::getUserDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Undertow");
    d.createDirectory();
    return d;
}

// presets live in the plugin's own menu; the host sees a single program so it can never
// reset every parameter behind the user's back
int UndertowProcessor::getNumPrograms() { return 1; }
const juce::String UndertowProcessor::getProgramName (int)
{
    auto p = getFactoryPresets();
    return juce::isPositiveAndBelow (currentPreset, (int) p.size()) ? p[(size_t) currentPreset].name : juce::String();
}
void UndertowProcessor::setCurrentProgram (int) {}

void UndertowProcessor::applyPreset (int index)
{
    auto presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size())) return;
    const auto& pr = presets[(size_t) index];
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ID::bypass)
                rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, v] : pr.values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (v));
    for (auto& s : Shapes::builtIn())
        if (s.id == pr.shapeId) setCurve (s.curve);
    currentPreset = index;
}

//==============================================================================
juce::ValueTree UndertowProcessor::stateWithExtras()
{
    auto st = apvts.copyState();
    for (auto t : { "CURVE", "KICK", "UI" })
        if (auto ch = st.getChildWithName (t); ch.isValid()) st.removeChild (ch, nullptr);
    juce::ValueTree cv ("CURVE");
    cv.setProperty ("pts", getCurve().toString(), nullptr);
    st.appendChild (cv, nullptr);
    st.setProperty ("preset", currentPreset, nullptr);
    return st;
}

juce::String UndertowProcessor::snapshot()
{
    return stateWithExtras().toXmlString (juce::XmlElement::TextFormat().singleLine());
}

void UndertowProcessor::restoreFromTree (const juce::ValueTree& tree)
{
    auto t = tree.createCopy();
    if (auto cv = t.getChildWithName ("CURVE"); cv.isValid())
    {
        setCurve (Curve::fromString (cv["pts"].toString()));
        t.removeChild (cv, nullptr);
    }
    if (auto k = t.getChildWithName ("KICK"); k.isValid())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (k["data"].toString()) && mb.getSize() >= sizeof (float))
        {
            const int len = (int) (mb.getSize() / sizeof (float));
            kickOriginal.setSize (1, len);
            std::memcpy (kickOriginal.getWritePointer (0), mb.getData(), (size_t) len * sizeof (float));
            kickOriginalRate = (double) k["rate"];
            kickName = k["name"].toString();
            rebuildKickForRate();
        }
        t.removeChild (k, nullptr);
    }
    if (auto u = t.getChildWithName ("UI"); u.isValid())
    {
        uiState.copyPropertiesFrom (u, nullptr);
        t.removeChild (u, nullptr);
    }
    if (t.hasProperty ("preset")) currentPreset = (int) t["preset"];
    apvts.replaceState (t);
    // make sure the host's view of every parameter matches the restored values
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getValue());
}

void UndertowProcessor::restoreSnapshot (const juce::String& s)
{
    if (auto xml = juce::parseXML (s))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid()) return;
        // apply parameters one by one so the host sees the changes
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                for (auto child : tree)
                    if (child.hasType ("PARAM") && child["id"].toString() == rp->getParameterID())
                    {
                        const float v = rp->convertTo0to1 ((float) child["value"]);
                        if (std::abs (rp->getValue() - v) > 1.0e-6f) rp->setValueNotifyingHost (v);
                    }
        if (auto cv = tree.getChildWithName ("CURVE"); cv.isValid())
            setCurve (Curve::fromString (cv["pts"].toString()));
        if (tree.hasProperty ("preset")) currentPreset = (int) tree["preset"];
    }
}

void UndertowProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = stateWithExtras();
    if (kickOriginal.getNumSamples() > 0)
    {
        juce::ValueTree k ("KICK");
        k.setProperty ("name", kickName, nullptr);
        k.setProperty ("rate", kickOriginalRate, nullptr);
        juce::MemoryBlock mb (kickOriginal.getReadPointer (0), (size_t) kickOriginal.getNumSamples() * sizeof (float));
        k.setProperty ("data", mb.toBase64Encoding(), nullptr);
        st.appendChild (k, nullptr);
    }
    st.appendChild (uiState.createCopy(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void UndertowProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        if (t.hasType (apvts.state.getType()))
        {
            if (! t.getChildWithName ("KICK").isValid()) clearKick();
            restoreFromTree (t);
        }
    }
}

juce::AudioProcessorEditor* UndertowProcessor::createEditor() { return new UndertowEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UndertowProcessor(); }
