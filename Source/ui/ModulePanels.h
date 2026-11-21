#pragma once

#include "ui/Knob.h"
#include "Parameters.h"

namespace ember { class EmberProcessor; }

namespace ember::ui
{
/** One card of the module row: a title, a 2-column grid of knobs and a row of pills. */
class ModuleCard : public juce::Component
{
public:
    ModuleCard (juce::String title, juce::String subtitle);
    void setContent (std::vector<juce::Component*> knobs, std::vector<juce::Component*> pills);
    void setStatus (const juce::String& s, juce::Colour c);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, subtitle, status;
    juce::Colour statusColour;
    std::vector<juce::Component*> knobs, pills;
};

/** The five EMBER modules: SPLIT (+SUB), HARMONICS (+TONE), WEIGHT, GLUE, OUTPUT. */
class ModulePanels : public juce::Component
{
public:
    explicit ModulePanels (EmberProcessor&);
    void resized() override;

    /** Called from the editor timer: macro rings and live status texts. */
    void refresh();
    void setAnimationsEnabled (bool);

private:
    Knob& knob (const char* id, const juce::String& label);
    TogglePill& toggle (const char* id, const juce::String& text, const juce::String& tip);

    EmberProcessor& proc;
    std::map<juce::String, std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<TogglePill>> toggles;
    std::unique_ptr<ChoicePill> phasePill, qualityPill;
    ModuleCard split { "SPLIT", "sub + corte" }, harmonics { "HARMONICS", "drive + tone" }, weight { "WEIGHT", "shelf grave" },
               glue { "GLUE", "compresor" }, output { "OUTPUT", "mono + mezcla" };
};

} // namespace ember::ui
