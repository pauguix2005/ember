#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ui/EmberLookAndFeel.h"

namespace ember { class EmberProcessor; }

namespace ember::ui
{
/** Harmonic analyzer: input ("antes") and output ("después") spectra on a log axis,
    with the area where the output is above the input filled in amber (the added
    harmonics). In Advanced mode it also draws the live WEIGHT curve with two
    draggable handles, and the SUB/BODY split. */
class Analyzer : public juce::Component, private juce::Timer
{
public:
    explicit Analyzer (EmberProcessor&);
    ~Analyzer() override;

    void setShowWeight (bool b) { showWeight = b; repaint(); }
    void setAnimationsEnabled (bool b) { animate = b; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = Handle::none; repaint(); }

private:
    enum class Handle { none, boost, atten };
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;

    void timerCallback() override;
    void computeSpectrum (const std::vector<float>& ring, int writePos, std::vector<float>& smoothed);
    juce::Path spectrumPath (const std::vector<float>& db, bool closeToBottom) const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float eqDbToY (float db) const;
    juce::Point<float> handlePos (Handle) const;
    Handle hitTest (juce::Point<float>) const;

    EmberProcessor& proc;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> preRing, postRing, fftData, preDb, postDb, scratch;
    int preWrite = 0, postWrite = 0;
    juce::Rectangle<float> plot;

    bool showWeight = false, animate = true;
    Handle hover = Handle::none, dragging = Handle::none;
    std::unique_ptr<juce::ParameterAttachment> freqAtt, boostAtt, attenAtt;
};

} // namespace ember::ui
