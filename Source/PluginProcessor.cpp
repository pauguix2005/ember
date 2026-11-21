#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ember
{
EmberProcessor::EmberProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "PARAMS", createParameterLayout()),
      refs (apvts)
{
    startTimerHz (15);
}

EmberProcessor::~EmberProcessor()
{
    stopTimer();
    cancelPendingUpdate();
}

bool EmberProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void EmberProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int numCh = std::max (1, std::min (2, getTotalNumOutputChannels()));
    engine.setParameters (getEffectiveParams());
    engine.prepare (sampleRate, samplesPerBlock, numCh);

    const int lat = engine.getLatencySamples();
    pendingLatency = lat;
    setLatencySamples (lat);

    inputCopy.setSize (2, samplesPerBlock);
    monoScratch.setSize (1, samplesPerBlock);
    const int maxLat = engine.latencyFor (PhaseMode::linearPhase, Quality::hq);
    for (auto& d : bypassDelay)
    {
        d.prepare (maxLat + 8);
        d.setDelay (lat);
    }
    bypassMix = refs.bypass->load() > 0.5f ? 1.0 : 0.0;
    bypassStep = 1.0 / (0.010 * sampleRate);   // ~10 ms crossfade

    widthFreq = 0.0f;
    widthCoeff = dsp::onePoleCoeff (0.3, sampleRate);
    levelCoeff = dsp::onePoleCoeff (0.05, sampleRate);
    for (int c = 0; c < 2; ++c)
    {
        for (int k = 0; k < 2; ++k) { widthMid[c][k].reset(); widthSide[c][k].reset(); }
        widthMidMs[c] = widthSideMs[c] = 0.0;
    }
}

void EmberProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

void EmberProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, true);
}

void EmberProcessor::measureLowWidth (const juce::AudioBuffer<float>& b, int n, bool isInput)
{
    const int k = isInput ? 0 : 1;
    if (b.getNumChannels() < 2)
    {
        (isInput ? lowWidthInDb : lowWidthOutDb) = -100.0f;
        return;
    }
    const float* l = b.getReadPointer (0);
    const float* r = b.getReadPointer (1);
    // measure well inside the mono region: 8th-order low-pass at 0.7 x the Mono Below frequency
    const float f = 0.7f * refs.monoFreq->load();
    if (std::abs (f - widthFreq) > 0.5f)
    {
        widthFreq = f;
        widthC.set (f, currentSampleRate.load());
    }
    for (int i = 0; i < n; ++i)
    {
        double lo, hi, lo2;
        widthMid[k][0].process (widthC, 0.5 * (l[i] + r[i]), lo, hi);
        widthMid[k][1].process (widthC, lo, lo2, hi);
        widthMidMs[k] += widthCoeff * (lo2 * lo2 - widthMidMs[k]);
        widthSide[k][0].process (widthC, 0.5 * (l[i] - r[i]), lo, hi);
        widthSide[k][1].process (widthC, lo, lo2, hi);
        widthSideMs[k] += widthCoeff * (lo2 * lo2 - widthSideMs[k]);
    }
    const float v = widthMidMs[k] < 1.0e-9 ? -100.0f
                                           : (float) (10.0 * std::log10 ((widthSideMs[k] + 1.0e-20) / widthMidMs[k]));
    (isInput ? lowWidthInDb : lowWidthOutDb) = v;
}

void EmberProcessor::process (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const int chs = std::min (2, std::min (numIn, buffer.getNumChannels()));
    const int total = buffer.getNumSamples();
    if (chs == 0 || total == 0)
        return;

    const auto base = refs.read();
    engine.setParameters (applyWarmthMacro (base));
    const bool bypassOn = forceBypass || refs.bypass->load() > 0.5f;

    const int lat = engine.getLatencySamples();
    if (lat != pendingLatency.load())
    {
        pendingLatency = lat;
        triggerAsyncUpdate();
    }
    for (auto& d : bypassDelay)
        d.setDelay (lat);

    const int maxN = inputCopy.getNumSamples();
    for (int start = 0; start < total; start += maxN)
    {
        const int n = std::min (maxN, total - start);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), chs, start, n);

        // ---- input side: copy for bypass, analyzer, meters ----
        auto* mono = monoScratch.getWritePointer (0);
        for (int c = 0; c < chs; ++c)
            inputCopy.copyFrom (c, 0, view, c, 0, n);
        double peak = 0.0;
        for (int i = 0; i < n; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < chs; ++c) m += inputCopy.getSample (c, i);
            mono[i] = m / (float) chs;
            peak = std::max (peak, (double) std::abs (mono[i]));
        }
        preFifo.push (mono, n);
        inLevel = std::max (peak, inLevel * std::pow (1.0 - levelCoeff, n));
        inLevelDb = (float) dsp::gainToDb (inLevel);
        if (chs == 2) measureLowWidth (inputCopy, n, true);

        // ---- EMBER ----
        engine.process (view);

        // ---- bypass crossfade against the latency-compensated input ----
        const double target = bypassOn ? 1.0 : 0.0;
        for (int i = 0; i < n; ++i)
        {
            if (bypassMix < target)      bypassMix = std::min (target, bypassMix + bypassStep);
            else if (bypassMix > target) bypassMix = std::max (target, bypassMix - bypassStep);
            for (int c = 0; c < chs; ++c)
            {
                const float dry = bypassDelay[c].process (inputCopy.getSample (c, i));
                if (bypassMix > 0.0)
                {
                    auto* d = view.getWritePointer (c);
                    d[i] = (float) (d[i] * (1.0 - bypassMix) + dry * bypassMix);
                }
            }
        }

        // ---- output side: analyzer, meters ----
        peak = 0.0;
        for (int i = 0; i < n; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < chs; ++c) m += view.getSample (c, i);
            mono[i] = m / (float) chs;
            peak = std::max (peak, (double) std::abs (mono[i]));
        }
        postFifo.push (mono, n);
        outLevel = std::max (peak, outLevel * std::pow (1.0 - levelCoeff, n));
        outLevelDb = (float) dsp::gainToDb (outLevel);
        if (chs == 2) measureLowWidth (view, n, false);
    }

    gainMatchDb = engine.getGainMatchDb();
    glueGrDb = engine.getGlueReductionDb();
}

void EmberProcessor::handleAsyncUpdate()
{
    setLatencySamples (pendingLatency.load());
}

void EmberProcessor::timerCallback()
{
    if (engine.linearPhaseFilterNeedsUpdate())
        engine.updateLinearPhaseFilter();
}

//==============================================================================
int EmberProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

void EmberProcessor::setCurrentProgram (int index)
{
    const auto& f = getFactoryPresets();
    if (juce::isPositiveAndBelow (index, (int) f.size()))
    {
        loadPreset (f[(size_t) index]);
        currentFactoryIndex = index;
    }
}

const juce::String EmberProcessor::getProgramName (int index)
{
    const auto& f = getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) f.size()) ? f[(size_t) index].name : juce::String();
}

void EmberProcessor::loadPreset (const Preset& p)
{
    undoManager.beginNewTransaction ("Load preset");
    auto values = p.params;
    // Presets define the sound, not the session's quality/latency choices.
    const auto current = refs.read();
    values.quality = current.quality;
    values.phaseMode = current.phaseMode;
    writeParams (apvts, values);
    currentPresetName = p.name;
    const auto& f = getFactoryPresets();
    for (size_t i = 0; i < f.size(); ++i)
        if (f[i].name == p.name)
            currentFactoryIndex = (int) i;
}

bool EmberProcessor::saveCurrentAsUserPreset (const juce::String& name)
{
    if (! saveUserPreset (name, refs.read()))
        return false;
    currentPresetName = name;
    return true;
}

std::vector<Preset> EmberProcessor::getAllPresets() const
{
    auto all = getFactoryPresets();
    for (auto& u : loadUserPresets())
        all.push_back (u);
    return all;
}

void EmberProcessor::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot)
        return;
    slots[activeSlot] = apvts.copyState();
    if (slots[slot].isValid())
        apvts.replaceState (slots[slot].createCopy());
    activeSlot = slot;
}

void EmberProcessor::copyActiveToOtherSlot()
{
    slots[1 - activeSlot] = apvts.copyState();
}

//==============================================================================
void EmberProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("EMBER_STATE");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("preset", currentPresetName, nullptr);
    root.setProperty ("slot", activeSlot, nullptr);
    root.setProperty ("program", currentFactoryIndex, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (uiState.createCopy(), nullptr);
    for (int s = 0; s < 2; ++s)
        if (slots[s].isValid())
        {
            juce::ValueTree wrap ("SLOT");
            wrap.setProperty ("index", s, nullptr);
            wrap.appendChild (slots[s].createCopy(), nullptr);
            root.appendChild (wrap, nullptr);
        }
    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void EmberProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("EMBER_STATE"))
        return;

    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);
    auto ui = root.getChildWithName ("UI");
    if (ui.isValid())
        uiState.copyPropertiesFrom (ui, nullptr);
    currentPresetName = root.getProperty ("preset", "Init Warm").toString();
    activeSlot = juce::jlimit (0, 1, (int) root.getProperty ("slot", 0));
    currentFactoryIndex = juce::jlimit (0, getNumPrograms() - 1, (int) root.getProperty ("program", 0));
    slots[0] = slots[1] = {};
    for (auto child : root)
        if (child.hasType ("SLOT") && child.getNumChildren() > 0)
            slots[juce::jlimit (0, 1, (int) child.getProperty ("index", 0))] = child.getChild (0).createCopy();
    undoManager.clearUndoHistory();
}

juce::AudioProcessorEditor* EmberProcessor::createEditor()
{
    return new EmberEditor (*this);
}

} // namespace ember

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ember::EmberProcessor();
}
