#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/Filters.h"
#include "dsp/Saturator.h"

namespace ember
{
/** The complete EMBER signal chain, independent of the plugin wrapper so it can be
    tested offline.

        IN ─┬─ SPLIT ─┬─ SUB ──────────── delay(os) ──┬──────────────┐
            │         │                               └─ sub harmonics ─┐
            │         └─ BODY ─ HARMONICS(os, ADAA) ─ DC ─────────(+)───┴─ TONE ─┐
            │                                                                    Σ ─ WEIGHT ─ GLUE ─ GAIN MATCH ─┐
            └─ dry: all-pass (or delay) matching the split + same latency ──────────────────────── MIX ─ MONO ─ OUT ─ CLIP

    Call setParameters() with the *effective* parameters (after the WARMTH macro)
    before every process() call. */
class EmberEngine
{
public:
    EmberEngine();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setParameters (const EngineParams& effective);
    void process (juce::AudioBuffer<float>& buffer);

    /** Latency of the mode currently requested by the parameters. */
    int getLatencySamples() const noexcept { return latencyFor (requestedPhase, requestedQuality); }
    int latencyFor (PhaseMode, Quality) const noexcept;

    /** Linear-phase crossover: the FIR is designed and loaded off the audio thread. */
    bool linearPhaseFilterNeedsUpdate() const noexcept;
    void updateLinearPhaseFilter();               // message thread (allocates)

    /** Smoothed gain-match correction in dB (for display). */
    float getGainMatchDb() const noexcept { return (float) gmDb.load(); }
    /** Current GLUE gain reduction in dB (positive). */
    float getGlueReductionDb() const noexcept { return (float) glueGrDisplay.load(); }

    static std::vector<float> designLinearPhaseLowpass (double cutoff, double sampleRate, int numTaps);
    static int linearPhaseTaps (double sampleRate);

private:
    static constexpr int subBlock = 32;

    struct Smoothed
    {
        double current = 0, target = 0, step = 0;
        int remaining = 0, rampLength = 1;
        void setRampLength (int n) { rampLength = std::max (1, n); }
        void snap (double v) { current = target = v; remaining = 0; step = 0; }
        void setTarget (double v)
        {
            if (dsp::same (v, target)) return;
            target = v;
            remaining = rampLength;
            step = (target - current) / rampLength;
        }
        /** Advances by n samples and returns the value at the start of the advance. */
        double advance (int n)
        {
            const double start = current;
            if (remaining > 0)
            {
                const int k = std::min (n, remaining);
                current += step * k;
                remaining -= k;
                if (remaining == 0) current = target;
            }
            return start;
        }
    };

    void processChunk (float* const* channels, int numSamples, int linOffset, int numChannelsToProcess);
    void applyPendingModeSwitch();
    void updateDelays();
    juce::dsp::Oversampling<float>* activeOversampler() const noexcept;

    double fs = 48000.0;
    int maxBlock = 512, numCh = 2;

    // Mode handling (phase mode and quality change latency: we fade out, switch, fade in)
    PhaseMode activePhase = PhaseMode::zeroLatency, requestedPhase = PhaseMode::zeroLatency;
    Quality activeQuality = Quality::normal, requestedQuality = Quality::normal;
    double modeFade = 1.0;
    bool fadingOut = false;
    int holdSamples = 0;
    double lastXo = -1;

    // Smoothed controls
    Smoothed sXover, sSubGain, sSubHarm, sDrive, sChar, sBias, sTone, sWFreq, sBoost, sAtten, sRes,
             sGlue, sAttack, sRelease, sMonoFreq, sMonoAmt, sMix, sOut;
    bool releaseAuto = true, gainMatchOn = true, softClipOn = false;

    // SPLIT
    dsp::SvfCoeffs xoC;
    dsp::LinkwitzRiley4 xo[2];
    dsp::AllPass2 dryAp[2];
    juce::dsp::Convolution linConv;
    juce::AudioBuffer<float> linSub;
    dsp::DelayLine linDelay[2];
    int linLatency = 0;
    std::atomic<float> linIrFreq { -1.0f }, linIrRequested { 120.0f };
    bool linIrLoaded = false;

    // HARMONICS
    std::unique_ptr<juce::dsp::Oversampling<float>> os2, os4;
    dsp::AdaaSaturator sat[2];
    dsp::DcBlocker dc[2];
    dsp::DelayLine subDelay[2], dryDelay[2];

    // Sub harmonics (octave-up and 3rd/4th generated from the clean sub, high-passed above the split)
    double subEnv[2] {}, envAtt = 0, envRel = 0;
    dsp::LinkwitzRiley4 shHp1[2], shHp2[2];
    dsp::Biquad shLp[2];

    // TONE
    dsp::SvfCoeffs toneC;
    dsp::SvfState toneA[2];

    // WEIGHT
    dsp::ShelfSvf wBoost[2], wAtten[2];

    // GLUE
    dsp::Biquad glueSc[2];
    double glueLevel = 0, glueFast = 0, glueSlow = 0;
    std::atomic<double> glueGrDisplay { 0.0 };

    // GAIN MATCH
    double dryMs = 0, wetMs = 0, gmDbCurrent = 0;
    std::atomic<double> gmDb { 0.0 };

    // OUTPUT
    dsp::SvfCoeffs monoC;
    dsp::LinkwitzRiley4 monoS, monoS2;   // 8th-order side high-pass
    dsp::AllPass2 monoM, monoM2;         // matching all-pass on mid
    dsp::SoftClipper clipper;

    // Scratch (allocated in prepare)
    juce::AudioBuffer<float> subBuf, bodyBuf, dryBuf;
};

} // namespace ember
