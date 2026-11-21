#include "ui/WarmthKnob.h"

namespace ember::ui
{
namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * -0.75f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 0.75f;
}

WarmthKnob::WarmthKnob (juce::RangedAudioParameter& p, juce::UndoManager* um)
    : Knob (p, um, "Warmth")
{
    setDescription ("Control principal: mueve Drive, Character, Weight, Glue y Tone a la vez. 40 % es neutro.");
    startTimerHz (60);
}

juce::Rectangle<int> WarmthKnob::valueArea() const
{
    auto c = getLocalBounds().toFloat().withTrimmedBottom (26).getCentre();
    return juce::Rectangle<int> (70, 26).withCentre (c.toInt());
}

void WarmthKnob::timerCallback()
{
    const float a = animate ? 0.18f : 1.0f;     // ~150–250 ms settle at 60 fps
    const float prevShown = shownValue, prevLevel = level;
    shownValue = shownValue < 0.0f ? value : shownValue + (value - shownValue) * a;
    level += (targetLevel - level) * (animate ? 0.12f : 1.0f);
    if (std::abs (prevShown - shownValue) > 1.0e-4f || std::abs (prevLevel - level) > 1.0e-3f)
        repaint();
}

void WarmthKnob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto labelR = bounds.removeFromBottom (26);
    const float d = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 8.0f;
    auto area = juce::Rectangle<float> (d, d).withCentre (bounds.getCentre());
    const auto centre = area.getCentre();
    const float stroke = d * 0.075f;
    const float radius = d * 0.5f - stroke;
    const float v = shownValue < 0.0f ? value : shownValue;
    const float angle = startAngle + v * (endAngle - startAngle);

    // glow: wider, fainter strokes behind the arc, brighter with more warmth
    if (v > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, angle, true);
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (colours::brasa.withAlpha (0.035f * v * (5 - i) + 0.01f * v));
            g.strokePath (arc, juce::PathStrokeType (stroke + i * stroke * 0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc, brasa → ámbar along its length (segments approximate a conic gradient)
    if (v > 0.002f)
    {
        const int segs = juce::jmax (2, (int) (v * 72));
        for (int s = 0; s < segs; ++s)
        {
            const float t0 = (float) s / segs, t1 = (float) (s + 1) / segs;
            juce::Path seg;
            seg.addCentredArc (centre.x, centre.y, radius, radius, 0,
                               startAngle + t0 * (angle - startAngle), startAngle + t1 * (angle - startAngle) + 0.01f, true);
            g.setColour (colours::brasa.interpolatedWith (colours::ambar, t1 * v));
            g.strokePath (seg, juce::PathStrokeType (stroke, juce::PathStrokeType::curved,
                                                     s == 0 || s == segs - 1 ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
        }
    }

    // neutral tick at 40 %
    {
        const float a = startAngle + 0.4f * (endAngle - startAngle);
        g.setColour (colours::textDim.withAlpha (0.7f));
        g.drawLine (juce::Line<float> (centre.getPointOnCircumference (radius + stroke * 0.7f, a),
                                       centre.getPointOnCircumference (radius + stroke * 1.15f, a)), 1.5f);
    }

    // core: dark disc with an ember glow that breathes with the input level
    const float coreR = radius - stroke * 0.9f;
    g.setColour (colours::panelRaised);
    g.fillEllipse (juce::Rectangle<float> (coreR * 2, coreR * 2).withCentre (centre));
    {
        const float heat = 0.15f + 0.85f * v;
        const float pulse = 0.55f + 0.45f * level;
        juce::ColourGradient cg (colours::brasa.withAlpha (0.30f * heat * pulse), centre.x, centre.y + coreR * 0.25f,
                                 colours::panelRaised.withAlpha (0.0f), centre.x, centre.y + coreR * (0.75f + 0.25f * level), true);
        g.setGradientFill (cg);
        g.fillEllipse (juce::Rectangle<float> (coreR * 2, coreR * 2).withCentre (centre));
    }
    g.setColour (hasKeyboardFocus (false) ? colours::ambar.withAlpha (0.7f) : colours::outline);
    g.drawEllipse (juce::Rectangle<float> (coreR * 2, coreR * 2).withCentre (centre), 1.0f);

    // pointer dot
    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (stroke * 0.55f, stroke * 0.55f)
                       .withCentre (centre.getPointOnCircumference (coreR - stroke * 0.7f, angle)));

    // value
    if (editor == nullptr)
    {
        g.setColour (colours::text);
        g.setFont (fonts::mono (d * 0.15f));
        g.drawText (juce::String (juce::roundToInt (value * 100.0f)) + " %",
                    area.withSizeKeepingCentre (d * 0.7f, d * 0.2f), juce::Justification::centred);
    }

    g.setFont (fonts::title (15.0f));
    g.setColour (colours::text);
    g.drawText ("WARMTH", labelR, juce::Justification::centred);
}

} // namespace ember::ui
