#include "PluginEditor.h"

namespace ember
{
using ui::colours::background;
using ui::operator""_u8;
namespace col = ui::colours;

//==============================================================================
void ui::Meters::setLevels (float i, float o)
{
    if (std::abs (i - in) > 0.2f || std::abs (o - out) > 0.2f)
    {
        in = i; out = o;
        repaint();
    }
}

void ui::Meters::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto labels = r.removeFromBottom (14.0f);
    const float barW = 12.0f;
    auto drawBar = [&] (juce::Rectangle<float> slot, float db, const char* name, juce::Rectangle<float> labelR)
    {
        auto bar = slot.withSizeKeepingCentre (barW, slot.getHeight());
        g.setColour (colours::panel);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (bar.reduced (0.5f), 3.0f, 1.0f);
        const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);   // -60 .. +6 dBFS
        auto fill = bar.reduced (2.0f);
        fill = fill.withTop (fill.getBottom() - fill.getHeight() * norm);
        g.setGradientFill (juce::ColourGradient (colours::ambar, bar.getX(), bar.getY(), colours::brasa, bar.getX(), bar.getBottom(), false));
        if (db > -0.3f) g.setColour (colours::rojo);
        g.fillRoundedRectangle (fill, 2.0f);
        // 0 dBFS tick
        const float y0 = bar.getBottom() - 2.0f - (bar.getHeight() - 4.0f) * (60.0f / 66.0f);
        g.setColour (colours::textDim.withAlpha (0.6f));
        g.drawHorizontalLine ((int) y0, bar.getX() - 2.0f, bar.getRight() + 2.0f);
        g.setFont (fonts::mono (9.0f));
        g.setColour (colours::textDim);
        g.drawText (name, labelR, juce::Justification::centred);
    };
    auto left = r.removeFromLeft (r.getWidth() * 0.5f);
    drawBar (left, in, "IN", labels.removeFromLeft (labels.getWidth() * 0.5f));
    drawBar (r, out, "OUT", labels);
}

void ui::WidthIndicator::setValues (float i, float o, bool monoOn, float monoHz)
{
    if (std::abs (i - in) > 0.5f || std::abs (o - out) > 0.5f || monoOn != on || std::abs (monoHz - hz) > 0.5f)
    {
        in = i; out = o; on = monoOn; hz = monoHz;
        repaint();
    }
}

void ui::WidthIndicator::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    // side/mid below 120 Hz: < -30 dB reads as mono
    auto state = [] (float db) { return db < -30.0f ? 0 : (db < -15.0f ? 1 : 2); };
    const int sOut = state (out), sIn = state (in);
    const char* names[] = { "MONO", "CASI MONO", "ANCHO" };
    const juce::Colour cols[] = { colours::text, colours::ambar, colours::rojo };

    auto inner = getLocalBounds().reduced (12, 0);
    g.setFont (fonts::mono (10.5f));
    g.setColour (colours::textDim);
    const auto prefix = on ? "Mono < " + juce::String (juce::roundToInt (hz)) + " Hz" : juce::String ("Mono off");
    g.drawText (prefix, inner, juce::Justification::centredLeft);
    g.setColour (cols[sOut]);
    g.drawText (juce::String (names[sIn]) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + names[sOut], inner, juce::Justification::centredRight);
}

//==============================================================================
EmberEditor::EmberEditor (EmberProcessor& p)
    : AudioProcessorEditor (p), proc (p), analyzer (p),
      warmth (*p.apvts.getParameter (pid::warmth), &p.undoManager), modules (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    for (auto* c : std::initializer_list<juce::Component*> { &analyzer, &warmth, &meters, &widthIndicator, &modules, &presetBox,
                                                             &prevPreset, &nextPreset, &savePreset, &slotA, &slotB, &copySlot,
                                                             &undoButton, &redoButton, &modeButton, &menuButton, &bypassButton })
        content.addAndMakeVisible (c);

    widthIndicator.setTooltip (juce::String::fromUTF8 ("¿Es mono el contenido por debajo de la frecuencia de Mono Below? Entrada → salida."));
    presetBox.setTitle ("Preset");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (idx, (int) presetList.size()) && presetList[(size_t) idx].name != shownPresetName)
        {
            proc.loadPreset (presetList[(size_t) idx]);
            shownPresetName = presetList[(size_t) idx].name;
        }
    };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    prevPreset.setTitle ("Preset anterior");
    nextPreset.setTitle ("Preset siguiente");
    savePreset.onClick = [this] { savePresetDialog(); };
    savePreset.setTooltip ("Guarda el sonido actual como preset de usuario (JSON).");

    slotA.setTooltip ("Ranura A");
    slotB.setTooltip ("Ranura B");
    copySlot.setTooltip ("Copia la ranura activa en la otra");
    slotA.onClick = [this] { proc.selectSlot (0); updateSlotButtons(); };
    slotB.onClick = [this] { proc.selectSlot (1); updateSlotButtons(); };
    copySlot.onClick = [this] { proc.copyActiveToOtherSlot(); };
    updateSlotButtons();

    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };

    modeButton.setClickingTogglesState (true);
    modeButton.setTooltip ("Simple: analizador y WARMTH. Avanzado: los cinco módulos."_u8);
    modeButton.onClick = [this] { setAdvanced (modeButton.getToggleState(), true); };
    menuButton.onClick = [this] { showSettingsMenu(); };
    menuButton.setTitle ("Ajustes");

    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.apvts.getParameter (pid::bypass), bypassButton, &p.undoManager);
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass con crossfade de 10 ms y latencia compensada.");

    rebuildPresetMenu();

    const bool adv = proc.isAdvancedMode();
    modeButton.setToggleState (adv, juce::dontSendNotification);
    setAdvanced (adv, false);
    const bool anim = proc.animationsEnabled();
    analyzer.setAnimationsEnabled (anim);
    warmth.setAnimationsEnabled (anim);
    modules.setAnimationsEnabled (anim);

    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / baseHeight);
    const float scale = juce::jlimit (0.75f, 2.0f, proc.getUiScale());
    setSize (juce::roundToInt (baseWidth * scale), juce::roundToInt (baseHeight * scale));

    startTimerHz (30);
}

EmberEditor::~EmberEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EmberEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::background);
}

void EmberEditor::resized()
{
    const float s = (float) getWidth() / (float) baseWidth;
    content.setBounds (0, 0, baseWidth, baseHeight);
    content.setTransform (juce::AffineTransform::scale (s));
    proc.setUiScale (s);
}

void EmberEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (col::background);
    // warm glow behind the WARMTH area
    const auto wb = warmth.getBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (col::brasa.withAlpha (0.07f), wb.getCentreX(), wb.getCentreY(),
                                             col::background.withAlpha (0.0f), wb.getCentreX(), wb.getCentreY() + wb.getHeight() * 0.8f, true));
    g.fillRect (wb.expanded (60.0f));

    // logo
    g.setColour (col::brasa);
    g.fillEllipse (16.0f, 19.0f, 9.0f, 9.0f);
    g.setColour (col::ambar.withAlpha (0.35f));
    g.drawEllipse (13.5f, 16.5f, 14.0f, 14.0f, 1.0f);
    g.setFont (ui::fonts::title (20.0f));
    g.setColour (col::text);
    g.drawText ("EMBER", 32, 8, 80, 32, juce::Justification::centredLeft);
    g.setColour (col::outline);
    g.drawHorizontalLine (46, 12.0f, (float) baseWidth - 12.0f);
}

void EmberEditor::layoutContent()
{
    // header
    prevPreset.setBounds (108, 12, 22, 26);
    presetBox.setBounds (134, 11, 230, 28);
    nextPreset.setBounds (368, 12, 22, 26);
    savePreset.setBounds (394, 12, 58, 26);
    undoButton.setBounds (462, 12, 70, 26);
    redoButton.setBounds (536, 12, 62, 26);
    slotA.setBounds (606, 12, 26, 26);
    slotB.setBounds (634, 12, 26, 26);
    copySlot.setBounds (662, 12, 38, 26);
    modeButton.setBounds (708, 12, 78, 26);
    menuButton.setBounds (790, 12, 26, 26);
    bypassButton.setBounds (822, 12, 66, 26);

    // body: interpolate between the Simple (a = 0) and Advanced (a = 1) layouts
    const float a = advancedAmount;
    auto lerp = [a] (juce::Rectangle<float> s, juce::Rectangle<float> d)
    {
        return juce::Rectangle<float> (s.getX() + (d.getX() - s.getX()) * a, s.getY() + (d.getY() - s.getY()) * a,
                                       s.getWidth() + (d.getWidth() - s.getWidth()) * a,
                                       s.getHeight() + (d.getHeight() - s.getHeight()) * a).toNearestInt();
    };
    analyzer.setBounds (lerp ({ 12, 56, 548, 452 }, { 12, 56, 548, 206 }));
    meters.setBounds (lerp ({ 570, 56, 44, 452 }, { 570, 56, 44, 206 }));
    warmth.setBounds (lerp ({ 624, 110, 264, 270 }, { 634, 54, 244, 176 }));
    widthIndicator.setBounds (lerp ({ 634, 404, 244, 26 }, { 634, 236, 244, 26 }));
    modules.setBounds (lerp ({ 12, 530, 876, 236 }, { 12, 272, 876, 236 }));
    modules.setAlpha (a);
    modules.setVisible (a > 0.001f);
}

void EmberEditor::setAdvanced (bool advanced, bool animateChange)
{
    advancedTarget = advanced ? 1.0f : 0.0f;
    if (! animateChange || ! proc.animationsEnabled())
        advancedAmount = advancedTarget;
    proc.setAdvancedMode (advanced);
    analyzer.setShowWeight (advanced);
    modeButton.setButtonText (advanced ? "Avanzado" : "Simple");
    layoutContent();
}

void EmberEditor::updateSlotButtons()
{
    slotA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
    copySlot.setButtonText (proc.getActiveSlot() == 0 ? "A>B" : "B>A");
}

void EmberEditor::timerCallback()
{
    meters.setLevels (proc.inLevelDb.load(), proc.outLevelDb.load());
    warmth.setInputLevelDb (proc.inLevelDb.load());
    modules.refresh();
    const auto base = proc.getBaseParams();
    widthIndicator.setValues (proc.lowWidthInDb.load(), proc.lowWidthOutDb.load(), base.monoOn, base.monoFreq);

    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());

    if (proc.getCurrentPresetName() != shownPresetName)
    {
        shownPresetName = proc.getCurrentPresetName();
        for (size_t i = 0; i < presetList.size(); ++i)
            if (presetList[i].name == shownPresetName)
                presetBox.setSelectedId ((int) i + 1, juce::dontSendNotification);
    }

    if (! juce::exactlyEqual (advancedAmount, advancedTarget))
    {
        const float step = (1.0f / 30.0f) / 0.2f;   // 200 ms transition
        advancedAmount = advancedTarget > advancedAmount ? juce::jmin (advancedTarget, advancedAmount + step)
                                                         : juce::jmax (advancedTarget, advancedAmount - step);
        layoutContent();
    }
}

void EmberEditor::rebuildPresetMenu()
{
    presetList = proc.getAllPresets();
    presetBox.clear (juce::dontSendNotification);
    const int numFactory = (int) getFactoryPresets().size();
    presetBox.addSectionHeading (juce::String::fromUTF8 ("Fábrica"));
    for (int i = 0; i < (int) presetList.size(); ++i)
    {
        if (i == numFactory)
        {
            presetBox.addSeparator();
            presetBox.addSectionHeading ("Usuario");
        }
        presetBox.addItem (presetList[(size_t) i].name, i + 1);
    }
    shownPresetName = proc.getCurrentPresetName();
    for (size_t i = 0; i < presetList.size(); ++i)
        if (presetList[i].name == shownPresetName)
            presetBox.setSelectedId ((int) i + 1, juce::dontSendNotification);
}

void EmberEditor::stepPreset (int delta)
{
    if (presetList.empty())
        return;
    int idx = presetBox.getSelectedId() - 1;
    idx = (idx + delta + (int) presetList.size()) % (int) presetList.size();
    presetBox.setSelectedId (idx + 1, juce::sendNotificationSync);
}

void EmberEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Guardar preset", "Nombre del preset de usuario:", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", proc.getCurrentPresetName() + " (mio)");
    w->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<EmberEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isNotEmpty() && safe->proc.saveCurrentAsUserPreset (name))
            safe->rebuildPresetMenu();
    }), true);
}

void EmberEditor::showSettingsMenu()
{
    juce::PopupMenu scaleMenu;
    const float current = proc.getUiScale();
    for (int pct : { 75, 100, 125, 150, 200 })
        scaleMenu.addItem (juce::String (pct) + " %", true, std::abs (current * 100.0f - pct) < 2.0f, [this, pct]
        {
            setSize (baseWidth * pct / 100, baseHeight * pct / 100);
        });

    juce::PopupMenu m;
    m.addSubMenu ("Escala de la ventana", scaleMenu);
    m.addItem ("Animaciones", true, proc.animationsEnabled(), [this]
    {
        const bool b = ! proc.animationsEnabled();
        proc.setAnimationsEnabled (b);
        analyzer.setAnimationsEnabled (b);
        warmth.setAnimationsEnabled (b);
        modules.setAnimationsEnabled (b);
    });
    m.addSeparator();
    m.addItem ("Abrir carpeta de presets", [] { auto d = getUserPresetDirectory(); d.createDirectory(); d.startAsProcess(); });
    m.addItem ("Recargar presets de usuario", [this] { rebuildPresetMenu(); });
    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("EMBER v" JucePlugin_VersionString " · calidez para bajos house"), false, false, [] {});
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton).withParentComponent (&content));
}

} // namespace ember
