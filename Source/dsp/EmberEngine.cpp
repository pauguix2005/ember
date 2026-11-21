#include "dsp/EmberEngine.h"

namespace ember
{
using namespace ember::dsp;

namespace
{
    constexpr double subHarmonicsGain = 4.0;     // calibrated in tests/EmberTests.cpp
    constexpr double maxBias = 0.6;             // input offset at Symmetry 0 %
    constexpr double dcCutoffHz = 5.0;
    constexpr double glueScHz = 80.0;

    /** Padé tanh (exact ±1 at |x| = 3), max error ~2e-2 — plenty for the sub-harmonic generator. */
    inline double fastTanh (double x)
    {
        x = std::clamp (x, -3.0, 3.0);
        const double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    }
    const double tanh08 = fastTanh (0.8);
}

EmberEngine::EmberEngine() = default;

int EmberEngine::linearPhaseTaps (double sampleRate)
{
    // ~170 ms of FIR at any rate: transition band ≈ 45 Hz (Blackman-Harris), latency ≈ 85 ms.
    int taps = (int) std::round (8192.0 * sampleRate / 48000.0);
    taps = juce::jlimit (2047, 32767, taps);
    return taps | 1; // odd → integer group delay
}

std::vector<float> EmberEngine::designLinearPhaseLowpass (double cutoff, double sampleRate, int numTaps)
{
    std::vector<double> h ((size_t) numTaps);
    const int mid = (numTaps - 1) / 2;
    const double fc = cutoff / sampleRate;
    double sum = 0.0;
    for (int n = 0; n < numTaps; ++n)
    {
        const int k = n - mid;
        const double sinc = k == 0 ? 2.0 * fc : std::sin (2.0 * pi * fc * k) / (pi * k);
        const double t = 2.0 * pi * n / (numTaps - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        h[(size_t) n] = sinc * w;
        sum += h[(size_t) n];
    }
    std::vector<float> out ((size_t) numTaps);
    for (int n = 0; n < numTaps; ++n)
        out[(size_t) n] = (float) (h[(size_t) n] / sum);   // unity gain at DC
    return out;
}

void EmberEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    numCh = juce::jlimit (1, 2, numChannels);

    const int ramp = (int) std::round (0.02 * fs);   // ~20 ms parameter smoothing
    for (auto* s : { &sXover, &sSubGain, &sSubHarm, &sDrive, &sChar, &sBias, &sTone, &sWFreq, &sBoost,
                     &sAtten, &sRes, &sGlue, &sAttack, &sRelease, &sMonoFreq, &sMonoAmt, &sMix, &sOut })
        s->setRampLength (ramp);
    // Big tonal moves on a low bass are smoother with a slightly longer glide.
    for (auto* s : { &sWFreq, &sBoost, &sAtten, &sRes, &sMonoFreq, &sMonoAmt })
        s->setRampLength ((int) std::round (0.05 * fs));

    using OS = juce::dsp::Oversampling<float>;
    os2 = std::make_unique<OS> ((size_t) numCh, 1, OS::filterHalfBandFIREquiripple, false, true);   // Normal: shorter FIR, lower CPU and latency
    os4 = std::make_unique<OS> ((size_t) numCh, 2, OS::filterHalfBandFIREquiripple, true, true);
    os2->initProcessing ((size_t) subBlock);
    os4->initProcessing ((size_t) subBlock);

    const int taps = linearPhaseTaps (fs);
    linLatency = (taps - 1) / 2;
    linIrFreq = -1.0f;
    updateLinearPhaseFilter();                      // load before prepare so it is active at once
    linConv.prepare ({ fs, (juce::uint32) maxBlock, (juce::uint32) numCh });
    linSub.setSize (numCh, maxBlock);

    const int maxOsLatency = (int) std::ceil (std::max (os2->getLatencyInSamples(), os4->getLatencyInSamples()));
    for (int c = 0; c < 2; ++c)
    {
        linDelay[c].prepare (linLatency + 8);
        subDelay[c].prepare (maxOsLatency + 8);
        dryDelay[c].prepare (maxOsLatency + 8);
        dc[c].setCutoff (dcCutoffHz, fs);
        glueSc[c].setHighPass (fs, glueScHz, 1.0 / sqrt2);
    }

    envAtt = onePoleCoeff (0.002, fs);
    envRel = onePoleCoeff (0.060, fs);

    subBuf.setSize (numCh, subBlock);
    bodyBuf.setSize (numCh, subBlock);
    dryBuf.setSize (numCh, subBlock);

    activePhase = requestedPhase;
    activeQuality = requestedQuality;
    for (auto* s : { &sXover, &sSubGain, &sSubHarm, &sDrive, &sChar, &sBias, &sTone, &sWFreq, &sBoost,
                     &sAtten, &sRes, &sGlue, &sAttack, &sRelease, &sMonoFreq, &sMonoAmt, &sMix, &sOut })
        s->snap (s->target);

    reset();
}

void EmberEngine::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        xo[c].reset(); dryAp[c].reset(); linDelay[c].reset(); subDelay[c].reset(); dryDelay[c].reset();
        sat[c].reset(); dc[c].reset(); shHp1[c].reset(); shHp2[c].reset(); shLp[c].reset();
        toneA[c].reset(); wBoost[c].reset(); wAtten[c].reset(); glueSc[c].reset();
        subEnv[c] = 0.0;
    }
    monoS.reset(); monoM.reset(); monoS2.reset(); monoM2.reset();
    if (os2 != nullptr) os2->reset();
    if (os4 != nullptr) os4->reset();
    linConv.reset();
    glueLevel = glueFast = glueSlow = 0.0;
    dryMs = wetMs = 0.0;
    gmDbCurrent = 0.0;
    gmDb = 0.0;
    modeFade = 1.0;
    fadingOut = false;
    holdSamples = 0;
    lastXo = -1;
    updateDelays();
}

juce::dsp::Oversampling<float>* EmberEngine::activeOversampler() const noexcept
{
    switch (activeQuality)
    {
        case Quality::normal: return os2.get();
        case Quality::hq:     return os4.get();
        case Quality::eco:    break;
    }
    return nullptr;
}

int EmberEngine::latencyFor (PhaseMode p, Quality q) const noexcept
{
    int lat = p == PhaseMode::linearPhase ? linLatency : 0;
    if (q == Quality::normal && os2 != nullptr) lat += (int) std::round (os2->getLatencyInSamples());
    if (q == Quality::hq && os4 != nullptr)     lat += (int) std::round (os4->getLatencyInSamples());
    return lat;
}

void EmberEngine::updateDelays()
{
    auto* os = activeOversampler();
    const int osLat = os != nullptr ? (int) std::round (os->getLatencyInSamples()) : 0;
    for (int c = 0; c < 2; ++c)
    {
        subDelay[c].setDelay (osLat);
        dryDelay[c].setDelay (osLat);
        linDelay[c].setDelay (linLatency);
    }
}

bool EmberEngine::linearPhaseFilterNeedsUpdate() const noexcept
{
    return std::abs (linIrFreq.load() - linIrRequested.load()) > 0.25f;
}

void EmberEngine::updateLinearPhaseFilter()
{
    const float f = linIrRequested.load();
    const int taps = linearPhaseTaps (fs);
    auto ir = designLinearPhaseLowpass (f, fs, taps);
    juce::AudioBuffer<float> b (1, taps);
    std::copy (ir.begin(), ir.end(), b.getWritePointer (0));
    linConv.loadImpulseResponse (std::move (b), fs, juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    linIrFreq = f;
}

void EmberEngine::setParameters (const EngineParams& p)
{
    if (p.phaseMode != requestedPhase || p.quality != requestedQuality)
    {
        requestedPhase = p.phaseMode;
        requestedQuality = p.quality;
    }
    if ((requestedPhase != activePhase || requestedQuality != activeQuality) && ! fadingOut)
        fadingOut = true;

    sXover.setTarget (p.xoverFreq);
    sSubGain.setTarget (dbToGain (p.subLevelDb));
    sSubHarm.setTarget (p.subHarmonics);
    sDrive.setTarget (p.driveDb);
    sChar.setTarget (p.character);
    sBias.setTarget ((1.0 - p.symmetry) * maxBias);
    sTone.setTarget (p.toneHz);
    sWFreq.setTarget (p.weightFreq);
    sBoost.setTarget (p.weightBoost);
    sAtten.setTarget (p.weightAtten);
    sRes.setTarget (p.weightRes);
    sGlue.setTarget (p.glueAmount);
    sAttack.setTarget (p.glueAttackMs);
    sRelease.setTarget (p.glueReleaseMs);
    sMonoFreq.setTarget (p.monoFreq);
    sMonoAmt.setTarget (p.monoOn ? 1.0 : 0.0);
    sMix.setTarget (p.mix);
    sOut.setTarget (dbToGain (p.outputDb));
    releaseAuto = p.glueReleaseAuto;
    gainMatchOn = p.gainMatch;
    softClipOn = p.softClip;
    linIrRequested = p.xoverFreq;
}

void EmberEngine::applyPendingModeSwitch()
{
    activePhase = requestedPhase;
    activeQuality = requestedQuality;
    for (int c = 0; c < 2; ++c)
    {
        xo[c].reset(); dryAp[c].reset(); sat[c].reset(); dc[c].reset();
        subDelay[c].reset(); dryDelay[c].reset();
    }
    if (os2 != nullptr) os2->reset();
    if (os4 != nullptr) os4->reset();
    updateDelays();
    fadingOut = false;
    // stay silent until every delay line and the FIR history are refilled
    holdSamples = latencyFor (activePhase, activeQuality) + 64;
}

void EmberEngine::process (juce::AudioBuffer<float>& buffer)
{
    const int total = buffer.getNumSamples();
    const int chs = std::min (buffer.getNumChannels(), numCh);
    if (chs == 0 || total == 0)
        return;

    float* ptrs[2] {};
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = std::min (maxBlock, total - start);
        for (int c = 0; c < chs; ++c)
            ptrs[c] = buffer.getWritePointer (c, start);

        const bool runConv = activePhase == PhaseMode::linearPhase || requestedPhase == PhaseMode::linearPhase;
        if (runConv)
        {
            for (int c = 0; c < chs; ++c)
                linSub.copyFrom (c, 0, ptrs[c], n);
            juce::dsp::AudioBlock<float> block (linSub.getArrayOfWritePointers(), (size_t) chs, (size_t) n);
            linConv.process (juce::dsp::ProcessContextReplacing<float> (block));
        }

        for (int off = 0; off < n; off += subBlock)
        {
            const int len = std::min (subBlock, n - off);
            float* chunk[2] {};
            for (int c = 0; c < chs; ++c)
                chunk[c] = ptrs[c] + off;
            processChunk (chunk, len, off, chs);
        }
    }
}

void EmberEngine::processChunk (float* const* ch, int n, int linOffset, int chs)
{
    if (fadingOut && modeFade <= 0.0)
        applyPendingModeSwitch();

    // ---- smoothed controls (start values for coefficients, start/end for gains) ----
    const double xoHz   = sXover.advance (n);
    const double subG0  = sSubGain.advance (n), subG1 = sSubGain.current;
    const double shAmt  = sSubHarm.advance (n), shAmt1 = sSubHarm.current;
    const double drv0   = sDrive.advance (n), drv1 = sDrive.current;
    const double chr    = sChar.advance (n);
    const double bias   = sBias.advance (n);
    const double toneHz = sTone.advance (n);
    const double wf     = sWFreq.advance (n);
    const double wb     = sBoost.advance (n);
    const double wa     = sAtten.advance (n);
    const double wr     = sRes.advance (n);
    const double glue   = sGlue.advance (n);
    const double attMs  = sAttack.advance (n);
    const double relMs  = sRelease.advance (n);
    const double monoHz = sMonoFreq.advance (n);
    const double mono0  = sMonoAmt.advance (n), mono1 = sMonoAmt.current;
    const double mix0   = sMix.advance (n), mix1 = sMix.current;
    const double out0   = sOut.advance (n), out1 = sOut.current;
    const double invN   = 1.0 / n;

    if (! same (xoHz, lastXo))
    {
        xoC.set (xoHz, fs);
        for (int c = 0; c < 2; ++c)
            shLp[c].setLowPass (fs, std::max (4.0 * xoHz, 400.0), 1.0 / sqrt2);
        lastXo = xoHz;
    }
    toneC.set (toneHz, fs);
    monoC.set (monoHz, fs);
    // WEIGHT: coefficients at both ends of the chunk, interpolated per sample
    const auto boostA = ShelfCoeffs::lowShelf (fs, wf, 0.6 + 1.0 * wr, wb);
    const auto attenA = ShelfCoeffs::lowShelf (fs, wf * 2.5, 0.5, -wa);
    const bool weightMoving = ! same (sWFreq.current, wf) || ! same (sBoost.current, wb) || ! same (sAtten.current, wa) || ! same (sRes.current, wr);
    const auto boostB = weightMoving ? ShelfCoeffs::lowShelf (fs, sWFreq.current, 0.6 + 1.0 * sRes.current, sBoost.current) : boostA;
    const auto attenB = weightMoving ? ShelfCoeffs::lowShelf (fs, sWFreq.current * 2.5, 0.5, -sAtten.current) : attenA;

    const bool linear = activePhase == PhaseMode::linearPhase;
    const bool subHarmActive = shAmt > 0.0 || sSubHarm.target > 0.0;
    if (! subHarmActive && subEnv[0] > 0.0)
        for (int c = 0; c < 2; ++c) { subEnv[c] = 0.0; shHp1[c].reset(); shHp2[c].reset(); shLp[c].reset(); }
    const double cDry = onePoleCoeff (1.5, fs);

    // ---- 1. SPLIT ----
    for (int c = 0; c < chs; ++c)
    {
        auto* sub = subBuf.getWritePointer (c);
        auto* body = bodyBuf.getWritePointer (c);
        auto* dry = dryBuf.getWritePointer (c);
        const float* lin = linSub.getReadPointer (c, linOffset);
        for (int i = 0; i < n; ++i)
        {
            const double x = ch[c][i];
            const double d = linDelay[c].process ((float) x);   // keep the FIR-aligned history warm
            double lo, hi;
            if (linear)
            {
                lo = lin[i];
                hi = d - lo;
                dry[i] = (float) d;
            }
            else
            {
                xo[c].process (xoC, x, lo, hi);
                dry[i] = (float) dryAp[c].process (xoC, x);
            }
            sub[i] = (float) lo;
            body[i] = (float) hi;
        }
    }
    for (int i = 0; i < n; ++i)
    {
        double p = 0.0;
        for (int c = 0; c < chs; ++c)
            p += (double) ch[c][i] * ch[c][i];
        dryMs += cDry * (p / chs - dryMs);
    }

    // ---- 2. HARMONICS on BODY (oversampled, ADAA) ----
    // Drive, bias and the curve blend are interpolated per sample so that fast
    // automation never steps the transfer curve (which would click).
    const auto blendA = CurveBlend::fromCharacter (chr), blendB = CurveBlend::fromCharacter (sChar.current);
    const double bias0 = bias, bias1 = sBias.current;
    const double g0 = dbToGain (drv0), g1 = dbToGain (drv1);
    const bool staticCurve = same (blendA.tube, blendB.tube) && same (blendA.iron, blendB.iron);
    const double fBiasStatic = blendA.f (bias0);

    auto saturate = [&] (float* data, int len, int c)
    {
        const double inv = 1.0 / len;
        for (int j = 0; j < len; ++j)
        {
            const double t = j * inv;
            const double g = g0 + (g1 - g0) * t;
            if (staticCurve && same (bias0, bias1))
            {
                data[j] = (float) ((sat[c].process (g * data[j] + bias0, blendA) - fBiasStatic) / g);
            }
            else if (staticCurve)
            {
                const double b = bias0 + (bias1 - bias0) * t;
                data[j] = (float) ((sat[c].process (g * data[j] + b, blendA) - blendA.f (b)) / g);
            }
            else
            {
                CurveBlend bl;
                bl.tube = blendA.tube + (blendB.tube - blendA.tube) * t;
                bl.tape = blendA.tape + (blendB.tape - blendA.tape) * t;
                bl.iron = blendA.iron + (blendB.iron - blendA.iron) * t;
                const double b = bias0 + (bias1 - bias0) * t;
                data[j] = (float) ((sat[c].process (g * data[j] + b, bl) - bl.f (b)) / g);
            }
        }
    };

    if (auto* os = activeOversampler())
    {
        juce::dsp::AudioBlock<float> block (bodyBuf.getArrayOfWritePointers(), (size_t) chs, (size_t) n);
        auto up = os->processSamplesUp (block);
        for (int c = 0; c < chs; ++c)
            saturate (up.getChannelPointer ((size_t) c), (int) up.getNumSamples(), c);
        os->processSamplesDown (block);
    }
    else
    {
        for (int c = 0; c < chs; ++c)
            saturate (bodyBuf.getWritePointer (c), n, c);
    }

    // ---- 3. align SUB/dry, sub harmonics, TONE, sum, WEIGHT ----
    for (int c = 0; c < chs; ++c)
    {
        auto* sub = subBuf.getWritePointer (c);
        auto* body = bodyBuf.getWritePointer (c);
        auto* dry = dryBuf.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const double s = subDelay[c].process (sub[i]);
            dry[i] = dryDelay[c].process (dry[i]);

            double b = dc[c].process (body[i]);

            // Sub harmonics: normalise the clean sub by its envelope, bend it asymmetrically
            // (2nd + 3rd + 4th), keep only what lies above the split and below ~4× the split.
            if (subHarmActive)
            {
                const double a = std::abs (s);
                subEnv[c] += (a > subEnv[c] ? envAtt : envRel) * (a - subEnv[c]);
                const double env = subEnv[c];
                const double u = std::clamp (s / (env + 1.0e-6), -1.5, 1.5);
                double h = (fastTanh (2.0 * u + 0.8) - tanh08) * env;
                h = shHp1[c].highOnly (xoC, h);
                h = shHp2[c].highOnly (xoC, h);
                h = shLp[c].process (h);
                b += h * (shAmt + (shAmt1 - shAmt) * i * invN) * subHarmonicsGain;
            }

            double lp, bp, hp;
            toneA[c].tick (toneC, b, lp, bp, hp);

            const double subGain = subG0 + (subG1 - subG0) * i * invN;
            double y = s * subGain + lp;
            if (weightMoving)
            {
                const double t = i * invN;
                y = wAtten[c].process (ShelfCoeffs::lerp (attenA, attenB, t),
                                       wBoost[c].process (ShelfCoeffs::lerp (boostA, boostB, t), y));
            }
            else
                y = wAtten[c].process (attenA, wBoost[c].process (boostA, y));
            body[i] = (float) y;
        }
    }

    // ---- 4. GLUE (stereo-linked, sidechain high-passed at 80 Hz) ----
    {
        const double cLvl = onePoleCoeff (0.005, fs);
        const double cAtt = onePoleCoeff (attMs * 0.001, fs);
        const double cRelFast = onePoleCoeff (releaseAuto ? 0.060 : relMs * 0.001, fs);
        const double cSlowAtt = onePoleCoeff (attMs * 0.004, fs);
        const double cSlowRel = onePoleCoeff (0.600, fs);
        const double thr = -12.0 - 18.0 * glue, ratio = 1.5 + glue, knee = 6.0;
        const double slope = 1.0 - 1.0 / ratio;
        const double cWet = cDry;

        for (int i = 0; i < n; ++i)
        {
            double det = 0.0, p = 0.0;
            for (int c = 0; c < chs; ++c)
            {
                const double v = bodyBuf.getSample (c, i);
                const double hpv = glueSc[c].process (v);
                det = std::max (det, hpv * hpv);
            }
            glueLevel += cLvl * (det - glueLevel);

            double target = 0.0;
            if (glue > 0.001 && glueLevel > 1.0e-10)
            {
                const double over = 10.0 * std::log10 (glueLevel) - thr;
                if (over >= knee * 0.5)       target = slope * over;
                else if (over > -knee * 0.5)  target = slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
            }
            glueFast += (target > glueFast ? cAtt : cRelFast) * (target - glueFast);
            double gr = glueFast;
            if (releaseAuto)
            {
                glueSlow += (target > glueSlow ? cSlowAtt : cSlowRel) * (target - glueSlow);
                gr = std::max (glueFast, glueSlow);
            }
            const float gain = gr > 1.0e-6 ? (float) std::exp (-gr * 0.11512925464970228) : 1.0f;
            for (int c = 0; c < chs; ++c)
            {
                auto* d = bodyBuf.getWritePointer (c);
                d[i] *= gain;
                p += (double) d[i] * d[i];
            }
            wetMs += cWet * (p / chs - wetMs);
        }
        glueGrDisplay = glueFast > glueSlow || ! releaseAuto ? glueFast : glueSlow;
    }

    // ---- 5. GAIN MATCH (slow loudness match of the wet path to the input) ----
    const double gmStart = gmDbCurrent;
    {
        double target = gmDbCurrent;
        if (! gainMatchOn)
            target = 0.0;
        else if (dryMs > 1.0e-7 && wetMs > 1.0e-12)          // gate: below −70 dBFS hold the last value
            target = std::clamp (10.0 * std::log10 (dryMs / wetMs), -12.0, 12.0);
        gmDbCurrent += onePoleCoeff (0.4, fs / n) * (target - gmDbCurrent);
        gmDb = gmDbCurrent;
    }
    const double gm0 = dbToGain (gmStart), gm1 = dbToGain (gmDbCurrent);

    // ---- 6. MIX, MONO BELOW, OUTPUT, SOFT CLIP, mode fade ----
    const double fadeStep = 1.0 / 256.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i * invN;
        const double mix = mix0 + (mix1 - mix0) * t;
        const double gm = gm0 + (gm1 - gm0) * t;
        double o[2] {};
        for (int c = 0; c < chs; ++c)
            o[c] = dryBuf.getSample (c, i) * (1.0 - mix) + bodyBuf.getSample (c, i) * gm * mix;

        if (chs == 2)
        {
            const double amt = mono0 + (mono1 - mono0) * t;
            const double m = 0.5 * (o[0] + o[1]), s = 0.5 * (o[0] - o[1]);
            const double mAp = monoM2.process (monoC, monoM.process (monoC, m));
            const double sHp = monoS2.highOnly (monoC, monoS.highOnly (monoC, s));
            const double mo = m + amt * (mAp - m);
            const double so = s + amt * (sHp - s);
            o[0] = mo + so;
            o[1] = mo - so;
        }

        double fade = modeFade;
        if (fadingOut)                 modeFade = std::max (0.0, modeFade - fadeStep);
        else if (holdSamples > 0)      { --holdSamples; fade = modeFade = 0.0; }
        else if (modeFade < 1.0)       modeFade = std::min (1.0, modeFade + fadeStep);

        const double outG = (out0 + (out1 - out0) * t) * fade;
        for (int c = 0; c < chs; ++c)
        {
            double y = o[c] * outG;
            if (softClipOn)
                y = clipper.process (y);
            ch[c][i] = (float) y;
        }
    }
}

} // namespace ember
