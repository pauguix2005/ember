#include "ui/Knob.h"

namespace ember::ui
{
namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * -0.75f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 0.75f;

    class KnobValueInterface : public juce::AccessibilityRangedNumericValueInterface
    {
    public:
        explicit KnobValueInterface (Knob& k) : knob (k) {}
        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return knob.getNormalisedValue(); }
        void setValue (double v) override { knob.setNormalisedValue ((float) v, true); }
        AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, 0.01 }; }
    private:
        Knob& knob;
    };
}

Knob::Knob (juce::RangedAudioParameter& p, juce::UndoManager* um, const juce::String& l)
    : param (p),
      attachment (p, [this] (float denorm) { value = param.convertTo0to1 (denorm); repaint(); }, um),
      label (l)
{
    setWantsKeyboardFocus (true);
    setTitle (p.getName (64));
    setDescription ("Arrastra o usa las flechas (Shift = fino). Doble clic: valor por defecto. Doble clic en el valor: escribir."_u8);
    attachment.sendInitialUpdate();
}

Knob::~Knob() = default;

void Knob::setModulatedValue (float n)
{
    if (std::abs (n - modValue) > 1.0e-4f)
    {
        modValue = n;
        repaint();
    }
}

juce::Rectangle<float> Knob::knobArea() const
{
    auto b = getLocalBounds().toFloat();
    const float d = juce::jmin (b.getWidth() - 4.0f, b.getHeight() - 30.0f);
    return { b.getCentreX() - d * 0.5f, 2.0f, d, d };
}

juce::Rectangle<int> Knob::valueArea() const
{
    return getLocalBounds().removeFromBottom (14);
}

void Knob::resized()
{
    if (editor != nullptr)
        editor->setBounds (valueArea().expanded (2, 1));
}

void Knob::paint (juce::Graphics& g)
{
    auto area = knobArea();
    const auto centre = area.getCentre();
    const float radius = area.getWidth() * 0.5f - 3.0f;
    const float stroke = juce::jmax (3.0f, radius * 0.16f);
    const float angle = startAngle + value * (endAngle - startAngle);
    const bool hot = isMouseOverOrDragging() || hasKeyboardFocus (false);

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    if (value > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, angle, true);
        const bool warn = value > warnAbove;
        if (warn)
            g.setColour (colours::rojo);
        else
            g.setGradientFill (juce::ColourGradient (colours::brasa, area.getBottomLeft(), colours::ambar, area.getTopRight(), false));
        g.strokePath (arc, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // macro (WARMTH) indicator: thin amber arc from the knob value to the effective value
    if (modValue >= 0.0f && std::abs (modValue - value) > 0.004f)
    {
        const float r2 = radius + stroke * 0.5f + 2.5f;
        const float a2 = startAngle + modValue * (endAngle - startAngle);
        juce::Path m;
        m.addCentredArc (centre.x, centre.y, r2, r2, 0, juce::jmin (angle, a2), juce::jmax (angle, a2), true);
        g.setColour (colours::ambar.withAlpha (0.85f));
        g.strokePath (m, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto dot = centre.getPointOnCircumference (r2, a2);
        g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre (dot));
    }

    // body + pointer
    const float bodyR = radius - stroke * 0.5f - 3.0f;
    g.setColour (hot ? colours::panelRaised.brighter (0.06f) : colours::panelRaised);
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (centre));
    g.setColour (colours::outline);
    g.drawEllipse (juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (centre), 1.0f);
    g.setColour (colours::text);
    g.drawLine (juce::Line<float> (centre.getPointOnCircumference (bodyR * 0.35f, angle),
                                   centre.getPointOnCircumference (bodyR * 0.9f, angle)), 2.0f);

    if (hasKeyboardFocus (false))
    {
        g.setColour (colours::ambar.withAlpha (0.5f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

    // label + value (value is always visible; brighter while dragging)
    auto b = getLocalBounds();
    auto valueR = b.removeFromBottom (14);
    auto labelR = b.removeFromBottom (14);
    g.setFont (fonts::title (11.5f));
    g.setColour (colours::textDim);
    g.drawFittedText (label.toUpperCase(), labelR, juce::Justification::centred, 1);
    if (editor == nullptr)
    {
        g.setFont (fonts::mono (11.0f));
        g.setColour (dragging || hot ? colours::ambar : colours::text);
        g.drawFittedText (param.getCurrentValueAsText(), valueR, juce::Justification::centred, 1);
    }
}

void Knob::setNormalisedValue (float v, bool asGesture)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    const float denorm = param.convertFrom0to1 (v);
    if (asGesture)
        attachment.setValueAsCompleteGesture (denorm);
    else
        attachment.setValueAsPartOfGesture (denorm);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showTextEntry();
        return;
    }
    dragging = true;
    lastDragY = e.position.y;
    attachment.beginGesture();
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const float dy = lastDragY - e.position.y;
    lastDragY = e.position.y;
    const float sensitivity = e.mods.isShiftDown() ? 0.0008f : 0.005f;   // Shift = fine adjust
    setNormalisedValue (value + dy * sensitivity, false);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        attachment.endGesture();
        dragging = false;
        repaint();
    }
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (valueArea().contains (e.getPosition()))
        showTextEntry();
    else
        setNormalisedValue (param.getDefaultValue(), true);
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() ? 0.002f : 0.02f) * (w.deltaY + w.deltaX >= 0 ? 1.0f : -1.0f);
    if (std::abs (w.deltaY) + std::abs (w.deltaX) > 0.0f)
        setNormalisedValue (value + step, true);
}

bool Knob::keyPressed (const juce::KeyPress& k)
{
    const float step = k.getModifiers().isShiftDown() ? 0.002f : 0.02f;
    if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::rightKey)) { setNormalisedValue (value + step, true); return true; }
    if (k.isKeyCode (juce::KeyPress::downKey) || k.isKeyCode (juce::KeyPress::leftKey)) { setNormalisedValue (value - step, true); return true; }
    if (k.isKeyCode (juce::KeyPress::returnKey)) { showTextEntry(); return true; }
    if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey))
    {
        setNormalisedValue (param.getDefaultValue(), true);
        return true;
    }
    return false;
}

void Knob::showTextEntry()
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (fonts::mono (11.0f));
    editor->setJustification (juce::Justification::centred);
    editor->setText (param.getCurrentValueAsText(), false);
    editor->selectAll();
    editor->setBounds (valueArea().expanded (2, 1));
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();

    auto commit = [this]
    {
        if (editor == nullptr) return;
        const auto text = editor->getText();
        juce::Component::SafePointer<Knob> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->editor.reset(); safe->repaint(); } });
        setNormalisedValue (param.getValueForText (text), true);
    };
    editor->onReturnKey = commit;
    editor->onFocusLost = commit;
    editor->onEscapeKey = [this]
    {
        juce::Component::SafePointer<Knob> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->editor.reset(); safe->repaint(); } });
    };
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> Knob::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces { std::make_unique<KnobValueInterface> (*this) });
}

//==============================================================================
ChoicePill::ChoicePill (juce::RangedAudioParameter& p, juce::UndoManager* um, juce::StringArray shortNames)
    : param (p),
      attachment (p, [this] (float v) { index = juce::roundToInt (v); repaint(); }, um),
      names (std::move (shortNames))
{
    setWantsKeyboardFocus (true);
    setTitle (p.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

void ChoicePill::cycle()
{
    attachment.setValueAsCompleteGesture ((float) ((index + 1) % names.size()));
}

void ChoicePill::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isMouseOver() ? colours::panelRaised.brighter (0.08f) : colours::panelRaised);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (hasKeyboardFocus (false) ? colours::ambar : colours::outline);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (index > 0 ? colours::ambar : colours::text);
    g.setFont (fonts::title (11.0f));
    g.drawFittedText (names[index], getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
}

void ChoicePill::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()))
        cycle();
}

bool ChoicePill::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::returnKey) || k.isKeyCode (juce::KeyPress::spaceKey))
    {
        cycle();
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> ChoicePill::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::button,
        juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [this] { cycle(); }));
}

//==============================================================================
TogglePill::TogglePill (juce::RangedAudioParameter& p, juce::UndoManager* um, const juce::String& text)
    : juce::TextButton (text), attachment (p, *this, um)
{
    setClickingTogglesState (true);
    setTitle (p.getName (64));
}

} // namespace ember::ui
