#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Presets.h"
#include "dsp/EmberEngine.h"

namespace ember
{
/** Single-producer / single-consumer mono sample FIFO for the analyzer. */
class AnalyzerFifo
{
public:
    void push (const float* data, int n)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        if (n1 > 0) std::copy (data, data + n1, buffer.begin() + s1);
        if (n2 > 0) std::copy (data + n1, data + n1 + n2, buffer.begin());
        fifo.finishedWrite (n1 + n2);
    }
    int pull (float* dest, int maxN)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxN, s1, n1, s2, n2);
        if (n1 > 0) std::copy (buffer.begin() + s1, buffer.begin() + s1 + n1, dest);
        if (n2 > 0) std::copy (buffer.begin(), buffer.begin() + n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }
private:
    static constexpr int size = 1 << 15;
    juce::AbstractFifo fifo { size };
    std::array<float, size> buffer {};
};

class EmberProcessor : public juce::AudioProcessor,
                       private juce::AsyncUpdater,
                       private juce::Timer
{
public:
    EmberProcessor();
    ~EmberProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "EMBER"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentFactoryIndex; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (pid::bypass); }

    //==============================================================================
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    EngineParams getBaseParams() const      { return refs.read(); }
    EngineParams getEffectiveParams() const { return applyWarmthMacro (refs.read()); }

    // Presets
    void loadPreset (const Preset& p);
    juce::String getCurrentPresetName() const { return currentPresetName; }
    bool saveCurrentAsUserPreset (const juce::String& name);
    std::vector<Preset> getAllPresets() const;     // factory first, then user

    // A/B comparison: two complete parameter snapshots
    int getActiveSlot() const noexcept { return activeSlot; }
    void selectSlot (int slot);
    void copyActiveToOtherSlot();

    // UI state persisted with the session
    bool  isAdvancedMode() const     { return uiState.getProperty ("advanced", false); }
    void  setAdvancedMode (bool b)   { uiState.setProperty ("advanced", b, nullptr); }
    float getUiScale() const         { return (float) uiState.getProperty ("scale", 1.0f); }
    void  setUiScale (float s)       { uiState.setProperty ("scale", s, nullptr); }
    bool  animationsEnabled() const  { return uiState.getProperty ("animations", true); }
    void  setAnimationsEnabled (bool b) { uiState.setProperty ("animations", b, nullptr); }

    // Visualisation feeds (audio thread → UI)
    AnalyzerFifo preFifo, postFifo;
    std::atomic<float> inLevelDb { -100.0f }, outLevelDb { -100.0f };
    std::atomic<float> lowWidthInDb { -100.0f }, lowWidthOutDb { -100.0f };   // side/mid below 120 Hz
    std::atomic<float> gainMatchDb { 0.0f }, glueGrDb { 0.0f };
    double getSampleRateSafe() const { return currentSampleRate.load(); }

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void process (juce::AudioBuffer<float>&, bool forceBypass);
    void measureLowWidth (const juce::AudioBuffer<float>&, int numSamples, bool isInput);

    ParameterRefs refs;
    EmberEngine engine;
    std::atomic<int> pendingLatency { 0 };
    std::atomic<double> currentSampleRate { 48000.0 };

    // latency-compensated dry for the bypass crossfade
    juce::AudioBuffer<float> inputCopy, monoScratch;
    dsp::DelayLine bypassDelay[2];
    double bypassMix = 0.0, bypassStep = 0.0;

    // low-band width meter state
    dsp::SvfCoeffs widthC;
    dsp::LinkwitzRiley4 widthMid[2][2], widthSide[2][2];   // 8th-order low-pass, [in/out][stage]
    float widthFreq = 0.0f;
    double widthMidMs[2] {}, widthSideMs[2] {}, widthCoeff = 0.0;
    double inLevel = 0.0, outLevel = 0.0, levelCoeff = 0.0;

    juce::String currentPresetName { "Init Warm" };
    int currentFactoryIndex = 0;
    int activeSlot = 0;
    juce::ValueTree slots[2];
    juce::ValueTree uiState { "UI" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberProcessor)
};

} // namespace ember
