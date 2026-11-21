#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ui/EmberLookAndFeel.h"

namespace ember::ui
{
/** Small parameter knob.
    Drag vertically (Shift = fine), mouse wheel, arrow keys.
    Double-click the knob to reset to default; double-click the value to type one. */
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::RangedAudioParameter& param, juce::UndoManager* um, const juce::String& label);
    ~Knob() override;

    /** Shows where the WARMTH macro has moved this control (normalised 0..1), or hides it (< 0). */
    void setModulatedValue (float normalised);
    /** Values above this (normalised) draw the arc in deep red — used for very high Drive. */
    void setWarningThreshold (float normalised) { warnAbove = normalised; repaint(); }
    void setAnimationsEnabled (bool b) { animate = b; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::RangedAudioParameter& getParameter() { return param; }
    float getNormalisedValue() const { return value; }
    void setNormalisedValue (float v, bool asGesture);

protected:
    virtual juce::Rectangle<int> valueArea() const;
    juce::Rectangle<float> knobArea() const;
    void showTextEntry();

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    float value = 0.0f, modValue = -1.0f, warnAbove = 2.0f;
    float lastDragY = 0.0f;
    bool dragging = false, animate = true;
    std::unique_ptr<juce::TextEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** Pill button that cycles through a choice parameter (e.g. "Zero Latency" / "Linear Phase"). */
class ChoicePill : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoicePill (juce::RangedAudioParameter& param, juce::UndoManager* um, juce::StringArray shortNames);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    int getIndex() const { return index; }
    void cycle();

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::StringArray names;
    int index = 0;
};

/** On/off pill bound to a bool parameter. */
class TogglePill : public juce::TextButton
{
public:
    TogglePill (juce::RangedAudioParameter& param, juce::UndoManager* um, const juce::String& text);
private:
    juce::ButtonParameterAttachment attachment;
};

} // namespace ember::ui
