#pragma once

#include "PluginProcessor.h"
#include "ui/EmberLookAndFeel.h"
#include "ui/Analyzer.h"
#include "ui/WarmthKnob.h"
#include "ui/ModulePanels.h"

namespace ember
{
namespace ui
{
    /** IN / OUT peak meters. */
    class Meters : public juce::Component
    {
    public:
        void setLevels (float inDb, float outDb);
        void paint (juce::Graphics&) override;
    private:
        float in = -100.0f, out = -100.0f;
    };

    /** Shows whether the content below 120 Hz is mono, before and after. */
    class WidthIndicator : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        void setValues (float inSideDb, float outSideDb, bool monoOn, float monoHz);
        void paint (juce::Graphics&) override;
    private:
        float in = -100.0f, out = -100.0f, hz = 120.0f;
        bool on = true;
    };
}

class EmberEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EmberEditor (EmberProcessor&);
    ~EmberEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseWidth = 900, baseHeight = 520;

private:
    /** Everything is laid out at 900 × 520 inside this component, which is scaled. */
    class Content : public juce::Component
    {
    public:
        explicit Content (EmberEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        void resized() override { owner.layoutContent(); }
    private:
        EmberEditor& owner;
    };

    void timerCallback() override;
    void paintContent (juce::Graphics&);
    void layoutContent();
    void rebuildPresetMenu();
    void stepPreset (int delta);
    void savePresetDialog();
    void showSettingsMenu();
    void setAdvanced (bool advanced, bool animateChange);
    void updateSlotButtons();

    EmberProcessor& proc;
    ui::EmberLookAndFeel lnf;
    Content content { *this };
    juce::TooltipWindow tooltips { this, 600 };

    ui::Analyzer analyzer;
    ui::WarmthKnob warmth;
    ui::Meters meters;
    ui::WidthIndicator widthIndicator;
    ui::ModulePanels modules;

    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, savePreset { "Guardar" };
    juce::TextButton slotA { "A" }, slotB { "B" }, copySlot { "A>B" };
    juce::TextButton undoButton { "Deshacer" }, redoButton { "Rehacer" };
    juce::TextButton modeButton { "Avanzado" }, menuButton { "..." };
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;

    std::vector<Preset> presetList;
    juce::String shownPresetName;
    float advancedAmount = 0.0f, advancedTarget = 0.0f;   // 0 = Simple, 1 = Advanced (animated)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberEditor)
};

} // namespace ember
