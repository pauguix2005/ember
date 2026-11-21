#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ember::ui
{
/** UTF-8 string literal → juce::String (JUCE treats plain char* as ASCII). */
inline juce::String operator""_u8 (const char* s, size_t n) { return juce::String::fromUTF8 (s, (int) n); }

/** "Brasa" visual direction. */
namespace colours
{
    inline const juce::Colour background  { 0xff14110f };   // warm black, not pure
    inline const juce::Colour panel       { 0xff1e1915 };   // module cards, analyzer
    inline const juce::Colour panelRaised { 0xff27201b };
    inline const juce::Colour outline     { 0xff342b24 };
    inline const juce::Colour track       { 0xff3a3029 };
    inline const juce::Colour brasa       { 0xffff7a2f };   // main accent
    inline const juce::Colour ambar       { 0xffffb347 };   // highlights / gradients
    inline const juce::Colour rojo        { 0xffc8372d };   // drive warning
    inline const juce::Colour text        { 0xfff2e8dc };
    inline const juce::Colour textDim     { 0xffa89888 };
    inline const juce::Colour before      { 0xff8c7f74 };   // analyzer "antes" curve
}

namespace fonts
{
    juce::Font title (float height);   // Space Grotesk
    juce::Font body (float height);    // Space Grotesk
    juce::Font mono (float height);    // JetBrains Mono (values)
}

class EmberLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EmberLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    juce::Font getLabelFont (juce::Label&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};

} // namespace ember::ui
