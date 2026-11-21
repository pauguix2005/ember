#include "ui/Analyzer.h"
#include "PluginProcessor.h"
#include "dsp/Filters.h"

namespace ember::ui
{
namespace
{
    constexpr float minF = 20.0f, maxF = 20000.0f;
    constexpr float minDb = -90.0f, maxDb = 0.0f;
    constexpr float eqRange = 15.0f;   // ±dB for the WEIGHT curve overlay
}

Analyzer::Analyzer (EmberProcessor& p) : proc (p)
{
    preRing.assign (fftSize, 0.0f);
    postRing.assign (fftSize, 0.0f);
    fftData.assign (2 * fftSize, 0.0f);
    preDb.assign (fftSize / 2, minDb);
    postDb.assign (fftSize / 2, minDb);
    scratch.assign (8192, 0.0f);

    auto& s = proc.apvts;
    freqAtt  = std::make_unique<juce::ParameterAttachment> (*s.getParameter (pid::weightFreq),  [this] (float) { repaint(); }, &proc.undoManager);
    boostAtt = std::make_unique<juce::ParameterAttachment> (*s.getParameter (pid::weightBoost), [this] (float) { repaint(); }, &proc.undoManager);
    attenAtt = std::make_unique<juce::ParameterAttachment> (*s.getParameter (pid::weightAtten), [this] (float) { repaint(); }, &proc.undoManager);

    setTitle ("Harmonic analyzer");
    setDescription ("Espectro de entrada y de salida. El área ámbar son los armónicos que añade EMBER."_u8);
    startTimerHz (60);
}

Analyzer::~Analyzer() { stopTimer(); }

void Analyzer::resized()
{
    plot = getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedTop (18.0f).withTrimmedBottom (12.0f);
}

float Analyzer::freqToX (float f) const
{
    return plot.getX() + plot.getWidth() * std::log (f / minF) / std::log (maxF / minF);
}
float Analyzer::xToFreq (float x) const
{
    return minF * std::exp ((x - plot.getX()) / plot.getWidth() * std::log (maxF / minF));
}
float Analyzer::dbToY (float db) const
{
    return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, plot.getBottom(), plot.getY());
}
float Analyzer::eqDbToY (float db) const
{
    return juce::jmap (db, -eqRange, eqRange, plot.getBottom(), plot.getY());
}

void Analyzer::timerCallback()
{
    auto pull = [this] (AnalyzerFifo& fifo, std::vector<float>& ring, int& pos)
    {
        int got;
        while ((got = fifo.pull (scratch.data(), (int) scratch.size())) > 0)
            for (int i = 0; i < got; ++i)
            {
                ring[(size_t) pos] = scratch[(size_t) i];
                pos = (pos + 1) & (fftSize - 1);
            }
    };
    pull (proc.preFifo, preRing, preWrite);
    pull (proc.postFifo, postRing, postWrite);
    computeSpectrum (preRing, preWrite, preDb);
    computeSpectrum (postRing, postWrite, postDb);
    repaint();
}

void Analyzer::computeSpectrum (const std::vector<float>& ring, int writePos, std::vector<float>& smoothed)
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = ring[(size_t) ((writePos + i) & (fftSize - 1))];
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float norm = 4.0f / fftSize;   // Hann: a full-scale sine reads 0 dB
    const float attack = animate ? 0.6f : 1.0f, release = animate ? 0.12f : 1.0f;
    for (int k = 0; k < fftSize / 2; ++k)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) k] * norm, minDb - 10.0f);
        auto& s = smoothed[(size_t) k];
        s += (db > s ? attack : release) * (db - s);
    }
}

juce::Path Analyzer::spectrumPath (const std::vector<float>& db, bool closeToBottom) const
{
    const double fs = proc.getSampleRateSafe();
    const float binHz = (float) (fs / fftSize);
    juce::Path p;
    bool started = false;
    const int steps = (int) plot.getWidth();
    for (int px = 0; px <= steps; px += 2)
    {
        const float x = plot.getX() + (float) px;
        const float f0 = xToFreq (x), f1 = xToFreq (x + 2.0f);
        int k0 = (int) std::floor (f0 / binHz), k1 = (int) std::ceil (f1 / binHz);
        k0 = juce::jlimit (1, fftSize / 2 - 1, k0);
        k1 = juce::jlimit (k0, fftSize / 2 - 1, k1);
        float v;
        if (k1 - k0 <= 1)
        {   // interpolate between bins in the sparse low end
            const float fb = f0 / binHz;
            const int kb = juce::jlimit (1, fftSize / 2 - 2, (int) fb);
            const float t = juce::jlimit (0.0f, 1.0f, fb - kb);
            v = db[(size_t) kb] + (db[(size_t) kb + 1] - db[(size_t) kb]) * t;
        }
        else
        {
            v = minDb - 10.0f;
            for (int k = k0; k <= k1; ++k) v = juce::jmax (v, db[(size_t) k]);
        }
        const float y = dbToY (v);
        if (! started) { p.startNewSubPath (x, y); started = true; }
        else           p.lineTo (x, y);
    }
    if (closeToBottom)
    {
        p.lineTo (plot.getRight(), plot.getBottom());
        p.lineTo (plot.getX(), plot.getBottom());
        p.closeSubPath();
    }
    return p;
}

juce::Point<float> Analyzer::handlePos (Handle h) const
{
    const auto b = proc.getBaseParams();
    if (h == Handle::boost) return { freqToX (b.weightFreq), eqDbToY (b.weightBoost) };
    return { freqToX (b.weightFreq * 2.5f), eqDbToY (-b.weightAtten) };
}

Analyzer::Handle Analyzer::hitTest (juce::Point<float> p) const
{
    if (! showWeight) return Handle::none;
    if (p.getDistanceFrom (handlePos (Handle::boost)) < 10.0f) return Handle::boost;
    if (p.getDistanceFrom (handlePos (Handle::atten)) < 10.0f) return Handle::atten;
    return Handle::none;
}

void Analyzer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // header: title + legend
    auto head = bounds.reduced (12.0f, 6.0f).removeFromTop (16.0f);
    g.setFont (fonts::body (12.5f));
    g.setColour (colours::textDim);
    g.drawText ("Analizador de armónicos"_u8, head, juce::Justification::centredLeft);
    {
        auto leg = head.removeFromRight (150.0f);
        auto item = [&g] (juce::Rectangle<float> r, juce::Colour c, const juce::String& t)
        {
            g.setColour (c);
            g.fillRoundedRectangle (r.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 2.5f), 1.0f);
            g.setColour (colours::textDim);
            g.drawText (t, r.withTrimmedLeft (5.0f), juce::Justification::centredLeft);
        };
        item (leg.removeFromLeft (64.0f), colours::before, "antes");
        item (leg, colours::ambar, "después"_u8);
    }

    // grid
    g.setFont (fonts::mono (9.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (f);
        g.setColour (colours::outline.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (colours::textDim.withAlpha (0.8f));
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 15.0f, plot.getBottom() + 1.0f, 30.0f, 11.0f), juce::Justification::centred);
    }
    for (float db : { -20.0f, -40.0f, -60.0f, -80.0f })
    {
        g.setColour (colours::outline.withAlpha (0.4f));
        g.drawHorizontalLine ((int) dbToY (db), plot.getX(), plot.getRight());
    }

    const auto eff = proc.getEffectiveParams();

    // SUB | BODY split
    {
        const float x = freqToX (eff.xoverFreq);
        const float dashes[] = { 3.0f, 3.0f };
        g.setColour (colours::textDim.withAlpha (0.45f));
        g.drawDashedLine (juce::Line<float> (x, plot.getY(), x, plot.getBottom()), dashes, 2, 1.0f);
        g.setFont (fonts::mono (9.0f));
        g.drawText ("SUB", juce::Rectangle<float> (x - 34.0f, plot.getY(), 30.0f, 11.0f), juce::Justification::centredRight);
        g.drawText ("BODY", juce::Rectangle<float> (x + 4.0f, plot.getY(), 34.0f, 11.0f), juce::Justification::centredLeft);
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    const auto prePath = spectrumPath (preDb, false);
    const auto postPath = spectrumPath (postDb, false);

    // amber fill where the output is above the input = harmonics EMBER added
    {
        g.saveState();
        juce::Path aboveBefore (prePath);
        aboveBefore.lineTo (plot.getRight(), plot.getY() - 2.0f);
        aboveBefore.lineTo (plot.getX(), plot.getY() - 2.0f);
        aboveBefore.closeSubPath();
        g.reduceClipRegion (aboveBefore);
        auto postFill = spectrumPath (postDb, true);
        g.setGradientFill (juce::ColourGradient (colours::ambar.withAlpha (0.45f), 0, plot.getY(),
                                                 colours::brasa.withAlpha (0.15f), 0, plot.getBottom(), false));
        g.fillPath (postFill);
        g.restoreState();
    }

    g.setColour (colours::before);
    g.strokePath (prePath, juce::PathStrokeType (1.2f));
    g.setColour (colours::ambar);
    g.strokePath (postPath, juce::PathStrokeType (1.8f));

    // WEIGHT curve
    if (showWeight)
    {
        const double fs = proc.getSampleRateSafe();
        dsp::Biquad bst, att;
        bst.setLowShelf (fs, eff.weightFreq, 0.6 + 1.0 * eff.weightRes, eff.weightBoost);
        att.setLowShelf (fs, eff.weightFreq * 2.5, 0.5, -eff.weightAtten);
        juce::Path eq;
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double f = xToFreq (x);
            const float db = (float) dsp::gainToDb (bst.magnitude (fs, f) * att.magnitude (fs, f));
            if (eq.isEmpty()) eq.startNewSubPath (x, eqDbToY (db));
            else                  eq.lineTo (x, eqDbToY (db));
        }
        const float dashes[] = { 2.0f, 4.0f };
        g.setColour (colours::textDim.withAlpha (0.35f));
        g.drawDashedLine (juce::Line<float> (plot.getX(), eqDbToY (0), plot.getRight(), eqDbToY (0)), dashes, 2, 1.0f);
        g.setColour (colours::text.withAlpha (0.85f));
        g.strokePath (eq, juce::PathStrokeType (1.6f));

        for (auto h : { Handle::boost, Handle::atten })
        {
            const auto p = handlePos (h);
            const bool active = hover == h || dragging == h;
            g.setColour (h == Handle::boost ? colours::brasa : colours::textDim);
            g.fillEllipse (juce::Rectangle<float> (active ? 12.0f : 9.0f, active ? 12.0f : 9.0f).withCentre (p));
            g.setColour (colours::background);
            g.drawEllipse (juce::Rectangle<float> (active ? 12.0f : 9.0f, active ? 12.0f : 9.0f).withCentre (p), 1.5f);
        }
        g.setFont (fonts::mono (9.0f));
        g.setColour (colours::textDim);
        g.drawText ("WEIGHT", plot.withTrimmedLeft (4.0f).removeFromBottom (12.0f).removeFromLeft (60.0f), juce::Justification::centredLeft);
    }
    g.restoreState();
}

void Analyzer::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h == Handle::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
        repaint();
    }
}

void Analyzer::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitTest (e.position);
    if (dragging == Handle::none)
        return;
    freqAtt->beginGesture();
    (dragging == Handle::boost ? boostAtt : attenAtt)->beginGesture();
}

void Analyzer::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Handle::none)
        return;
    float f = xToFreq (e.position.x);
    if (dragging == Handle::atten) f /= 2.5f;
    freqAtt->setValueAsPartOfGesture (juce::jlimit (20.0f, 150.0f, f));
    const float db = juce::jmap (e.position.y, plot.getBottom(), plot.getY(), -eqRange, eqRange);
    if (dragging == Handle::boost) boostAtt->setValueAsPartOfGesture (juce::jlimit (0.0f, 12.0f, db));
    else                           attenAtt->setValueAsPartOfGesture (juce::jlimit (0.0f, 12.0f, -db));
}

void Analyzer::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Handle::none)
        return;
    freqAtt->endGesture();
    (dragging == Handle::boost ? boostAtt : attenAtt)->endGesture();
    dragging = Handle::none;
}

} // namespace ember::ui
