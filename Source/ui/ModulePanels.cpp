#include "ui/ModulePanels.h"
#include "PluginProcessor.h"

namespace ember::ui
{
ModuleCard::ModuleCard (juce::String t, juce::String s) : title (std::move (t)), subtitle (std::move (s))
{
    setTitle (title);
}

void ModuleCard::setContent (std::vector<juce::Component*> k, std::vector<juce::Component*> p)
{
    knobs = std::move (k);
    pills = std::move (p);
    for (auto* c : knobs) addAndMakeVisible (c);
    for (auto* c : pills) addAndMakeVisible (c);
    resized();
}

void ModuleCard::setStatus (const juce::String& s, juce::Colour c)
{
    if (s != status || c != statusColour)
    {
        status = s;
        statusColour = c;
        repaint (getLocalBounds().removeFromTop (26));
    }
}

void ModuleCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    auto head = getLocalBounds().reduced (12, 0).removeFromTop (28);
    g.setFont (fonts::title (13.0f));
    g.setColour (colours::text);
    g.drawText (title, head, juce::Justification::centredLeft);
    g.setFont (fonts::mono (10.0f));
    g.setColour (status.isEmpty() ? colours::textDim.withAlpha (0.7f) : statusColour);
    g.drawText (status.isEmpty() ? subtitle : status, head, juce::Justification::centredRight);
}

void ModuleCard::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (28);
    r.removeFromBottom (8);
    const int rowH = 86;
    const int colW = r.getWidth() / 2;
    auto row1 = r.removeFromTop (rowH);
    auto row2 = r.removeFromTop (rowH);
    auto bottom = r.withTrimmedTop (4);

    std::vector<juce::Rectangle<int>> cells { row1.withWidth (colW), row1.withTrimmedLeft (colW),
                                              row2.withWidth (colW), row2.withTrimmedLeft (colW) };
    for (size_t i = 0; i < knobs.size() && i < cells.size(); ++i)
        knobs[i]->setBounds (cells[i].reduced (2, 0));

    if (pills.empty())
        return;
    if (knobs.size() == 3)
    {
        // pills stacked in the free 4th cell (and the bottom strip if needed)
        auto cell = cells[3].reduced (4, 6);
        const int h = juce::jmin (22, (cell.getHeight() - 4 * (int) (pills.size() - 1)) / (int) pills.size());
        auto stack = cell.withSizeKeepingCentre (cell.getWidth(), (int) pills.size() * (h + 4) - 4);
        for (auto* p : pills)
        {
            p->setBounds (stack.removeFromTop (h));
            stack.removeFromTop (4);
        }
    }
    else
    {
        const int w = (bottom.getWidth() - 4 * ((int) pills.size() - 1)) / (int) pills.size();
        auto strip = bottom.withHeight (22);
        for (auto* p : pills)
        {
            p->setBounds (strip.removeFromLeft (w));
            strip.removeFromLeft (4);
        }
    }
}

//==============================================================================
ModulePanels::ModulePanels (EmberProcessor& p) : proc (p)
{
    auto* um = &proc.undoManager;
    auto& s = proc.apvts;

    phasePill = std::make_unique<ChoicePill> (*s.getParameter (pid::phaseMode), um, juce::StringArray { "Zero Lat.", "Lin. Phase" });
    phasePill->setTooltip ("Zero Latency (por defecto) o Linear Phase. Linear Phase conserva la fase original pero añade ~85 ms de latencia, que el DAW compensa."_u8);
    qualityPill = std::make_unique<ChoicePill> (*s.getParameter (pid::quality), um, juce::StringArray { "Eco 1x", "Normal 2x", "HQ 4x" });
    qualityPill->setTooltip ("Oversampling del saturador (solo BODY). El ADAA está siempre activo."_u8);

    auto& xover = knob (pid::xoverFreq, "Corte");
    xover.setTooltip ("Separa SUB (debajo) y BODY (encima). Linkwitz-Riley 24 dB/oct."_u8);
    auto& subLevel = knob (pid::subLevel, "Sub");
    subLevel.setTooltip ("Nivel del sub limpio."_u8);
    auto& subHarm = knob (pid::subHarmonics, "Sub Harm.");
    subHarm.setTooltip ("Genera armónicos a partir del sub y los suma por encima del corte: un sub sinusoidal se oye en portátiles y móviles. El sub sigue limpio."_u8);
    split.setContent ({ &xover, &subLevel, &subHarm }, { phasePill.get() });

    auto& drive = knob (pid::drive, "Drive");
    drive.setTooltip ("Nivel de entrada al saturador de BODY."_u8);
    drive.setWarningThreshold (0.75f);
    auto& character = knob (pid::character, "Char");
    character.setTooltip ("Mezcla de tres curvas propias: Tube-ish (0 %), Tape-ish (50 %), Iron-ish (100 %)."_u8);
    auto& symmetry = knob (pid::symmetry, "Sym");
    symmetry.setTooltip ("100 % = solo armónicos impares. Menos añade pares (asimetría)."_u8);
    auto& tone = knob (pid::tone, "Tone");
    tone.setTooltip ("Paso bajo de 12 dB/oct tras saturar, para domar asperezas."_u8);
    harmonics.setContent ({ &drive, &character, &symmetry, &tone }, { qualityPill.get() });

    auto& wf = knob (pid::weightFreq, "Freq");
    wf.setTooltip ("Esquina del shelf grave estilo Pultec. También puedes arrastrar los puntos del analizador."_u8);
    auto& wb = knob (pid::weightBoost, "Boost");
    auto& wa = knob (pid::weightAtten, "Atten");
    wa.setTooltip ("Atenuación más ancha, con la esquina 2,5 veces más alta: abre el valle del low-mid."_u8);
    auto& wr = knob (pid::weightRes, "Reso");
    wr.setTooltip ("Joroba suave junto a la esquina del boost."_u8);
    weight.setContent ({ &wf, &wb, &wa, &wr }, {});

    auto& ga = knob (pid::glueAmount, "Amount");
    ga.setTooltip ("Un solo mando: ratio de 1,5:1 a 2,5:1 y su umbral. El sidechain ignora el sub (HPF 80 Hz)."_u8);
    auto& gat = knob (pid::glueAttack, "Attack");
    auto& grel = knob (pid::glueRelease, "Release");
    auto& autoRel = toggle (pid::glueReleaseAuto, "Auto", "Release dependiente del programa."_u8);
    glue.setContent ({ &ga, &gat, &grel }, { &autoRel });

    auto& mono = knob (pid::monoFreq, "Mono <");
    mono.setTooltip ("Todo lo que queda por debajo de esta frecuencia pasa a mono."_u8);
    auto& mix = knob (pid::mix, "Mix");
    mix.setTooltip ("Mezcla paralela. La ruta seca está alineada en fase: sin comb filtering."_u8);
    auto& out = knob (pid::output, "Output");
    auto& monoOn = toggle (pid::monoOn, "Mono", "Mono Below on/off.");
    auto& gm = toggle (pid::gainMatch, "Gain Match", "Iguala el nivel procesado al de entrada para comparar timbre, no volumen. Ayuda de escucha, no medidor certificado."_u8);
    auto& clip = toggle (pid::softClip, "Clip", "Recorte suave a -0,3 dBFS."_u8);
    output.setContent ({ &mono, &mix, &out }, { &monoOn, &gm, &clip });

    for (auto* c : { &split, &harmonics, &weight, &glue, &output })
        addAndMakeVisible (c);
}

Knob& ModulePanels::knob (const char* id, const juce::String& label)
{
    auto k = std::make_unique<Knob> (*proc.apvts.getParameter (id), &proc.undoManager, label);
    auto& ref = *k;
    knobs[id] = std::move (k);
    return ref;
}

TogglePill& ModulePanels::toggle (const char* id, const juce::String& text, const juce::String& tip)
{
    toggles.push_back (std::make_unique<TogglePill> (*proc.apvts.getParameter (id), &proc.undoManager, text));
    toggles.back()->setTooltip (tip);
    return *toggles.back();
}

void ModulePanels::resized()
{
    auto r = getLocalBounds();
    const int gap = 8;
    const int w = (r.getWidth() - 4 * gap) / 5;
    for (auto* c : { &split, &harmonics, &weight, &glue, &output })
    {
        c->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (gap);
    }
}

void ModulePanels::setAnimationsEnabled (bool b)
{
    for (auto& [id, k] : knobs)
        k->setAnimationsEnabled (b);
}

void ModulePanels::refresh()
{
    const auto eff = proc.getEffectiveParams();
    auto ring = [this] (const char* id, float plain)
    {
        auto& k = *knobs[id];
        k.setModulatedValue (k.getParameter().convertTo0to1 (plain));
    };
    ring (pid::drive, eff.driveDb);
    ring (pid::character, eff.character * 100.0f);
    ring (pid::weightBoost, eff.weightBoost);
    ring (pid::weightAtten, eff.weightAtten);
    ring (pid::glueAmount, eff.glueAmount * 100.0f);
    ring (pid::tone, eff.toneHz);

    const float gr = proc.glueGrDb.load();
    glue.setStatus (gr > 0.05f ? "GR -" + juce::String (gr, 1) + " dB" : juce::String(), colours::ambar);
    const auto base = proc.getBaseParams();
    output.setStatus (base.gainMatch ? "GM " + juce::String (proc.gainMatchDb.load(), 1) + " dB" : juce::String(), colours::textDim);
    harmonics.setStatus (eff.driveDb > 18.0f ? "DRIVE ALTO" : juce::String(), colours::rojo);
    split.setStatus (base.phaseMode == PhaseMode::linearPhase ? "+85 ms" : juce::String(), colours::textDim);
}

} // namespace ember::ui
