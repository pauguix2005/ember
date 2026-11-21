#pragma once

#include "Parameters.h"

namespace ember
{
struct Preset
{
    juce::String name;
    juce::String description;
    EngineParams params;   // base knob values (what gets written to the parameters)
};

/** The 12 factory presets of v1.0. They are authored as the *effective* sound
    (the numbers in the spec) and converted to base values for their WARMTH. */
const std::vector<Preset>& getFactoryPresets();

juce::var presetToJson (const juce::String& name, const EngineParams& p);
bool presetFromJson (const juce::var& json, juce::String& name, EngineParams& p);

juce::File getUserPresetDirectory();
std::vector<Preset> loadUserPresets();
bool saveUserPreset (const juce::String& name, const EngineParams& p);

} // namespace ember
