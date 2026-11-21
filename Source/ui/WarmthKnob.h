#pragma once

#include "ui/Knob.h"

namespace ember::ui
{
/** The big WARMTH macro: brasa→ámbar arc, glow that grows with the value and an
    ember core that breathes with the input level. */
class WarmthKnob : public Knob, private juce::Timer
{
public:
    WarmthKnob (juce::RangedAudioParameter& param, juce::UndoManager* um);

    void setInputLevelDb (float db) { targetLevel = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f); }
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> valueArea() const override;
    void timerCallback() override;

    float shownValue = -1.0f, level = 0.0f, targetLevel = 0.0f;
};

} // namespace ember::ui
