#include "ui/EmberLookAndFeel.h"
#include "BinaryData.h"

namespace ember::ui
{
namespace fonts
{
    static juce::Typeface::Ptr spaceGrotesk()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceGrotesk_ttf, BinaryData::SpaceGrotesk_ttfSize);
        return tf;
    }
    static juce::Typeface::Ptr jetBrains()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMono_ttf, BinaryData::JetBrainsMono_ttfSize);
        return tf;
    }

    juce::Font title (float h) { return juce::Font (juce::FontOptions (spaceGrotesk()).withHeight (h)).boldened(); }
    juce::Font body (float h)  { return juce::Font (juce::FontOptions (spaceGrotesk()).withHeight (h)); }
    juce::Font mono (float h)  { return juce::Font (juce::FontOptions (jetBrains()).withHeight (h)); }
}

EmberLookAndFeel::EmberLookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, brasa);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, ambar);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, brasa.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, panelRaised);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, brasa.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, brasa);
    setColour (juce::CaretComponent::caretColourId, ambar);
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, outline);
}

void EmberLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const bool on = b.getToggleState();
    auto fill = on ? colours::brasa : colours::panelRaised;
    if (! b.isEnabled())   fill = fill.withAlpha (0.4f);
    else if (down)         fill = fill.brighter (0.15f);
    else if (hi)           fill = fill.brighter (0.07f);
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (colours::ambar, r.getX(), r.getY(), colours::brasa, r.getX(), r.getBottom(), false));
        if (down) g.setColour (fill);
    }
    else
        g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    g.setColour (on ? colours::ambar.withAlpha (0.6f) : (hi ? colours::textDim.withAlpha (0.5f) : colours::outline));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

void EmberLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? colours::background : (b.isEnabled() ? colours::text : colours::textDim));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
}

juce::Font EmberLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return fonts::title (juce::jmin (13.0f, h * 0.52f));
}

void EmberLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.isMouseOver (true) ? colours::textDim.withAlpha (0.6f) : colours::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 18.0f, ay = h * 0.5f;
    arrow.startNewSubPath (ax - 4, ay - 2);
    arrow.lineTo (ax, ay + 2);
    arrow.lineTo (ax + 4, ay - 2);
    g.setColour (colours::ambar);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font EmberLookAndFeel::getComboBoxFont (juce::ComboBox&) { return fonts::title (14.0f); }

void EmberLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centred);
}

juce::Font EmberLookAndFeel::getPopupMenuFont() { return fonts::body (14.0f); }

void EmberLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::panelRaised);
    g.setColour (colours::outline);
    g.drawRect (0, 0, w, h);
}

juce::Font EmberLookAndFeel::getLabelFont (juce::Label& l)
{
    return fonts::body ((float) juce::jmin (14, l.getHeight() - 2));
}

void EmberLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (fonts::body (13.0f));
    g.drawFittedText (text, r.toNearestInt().reduced (8, 4), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> EmberLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (fonts::body (13.0f), tipText, 0, 0);
    const int w = juce::jmin (320, (int) ga.getBoundingBox (0, -1, true).getWidth() + 18);
    const int lines = 1 + (int) ga.getBoundingBox (0, -1, true).getWidth() / 300;
    const int h = 10 + 17 * lines;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h)
        .constrainedWithin (parent);
}

} // namespace ember::ui
