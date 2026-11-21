// Loads the built EMBER.vst3 through JUCE's VST3 hosting code and stress-tests it.
// This is NOT pluginval; it is a smaller set of similar checks.
// Usage: EmberHostTest <path/to/EMBER.vst3>
#include <juce_audio_processors/juce_audio_processors.h>
#include <thread>

namespace
{
int failures = 0;
void report (bool ok, const juce::String& name, const juce::String& detail = {})
{
    if (! ok) ++failures;
    std::printf ("[%s] %-50s %s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.toRawUTF8());
    std::fflush (stdout);
}

std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& fm, const juce::String& path, double sr, int block)
{
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType (path))
            f->findAllTypesForFile (types, path);
    if (types.isEmpty())
        return {};
    juce::String err;
    auto inst = fm.createPluginInstance (*types[0], sr, block, err);
    if (inst == nullptr)
        std::printf ("  load error: %s\n", err.toRawUTF8());
    return inst;
}

/** Processes `seconds` of noisy bass; returns false on NaN/Inf or absurd levels. */
bool run (juce::AudioPluginInstance& p, double sr, int block, double seconds, bool randomBlockSizes,
          bool fuzzParams, juce::Random& rnd, float& peakOut)
{
    const int ch = p.getTotalNumOutputChannels();
    juce::AudioBuffer<float> buf (std::max (ch, p.getTotalNumInputChannels()), block);
    juce::MidiBuffer midi;
    const int total = (int) (seconds * sr);
    double phase = 0;
    peakOut = 0;
    auto params = p.getParameters();
    for (int done = 0; done < total;)
    {
        const int n = randomBlockSizes ? 1 + rnd.nextInt (block) : block;
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), buf.getNumChannels(), 0, n);
        for (int i = 0; i < n; ++i)
        {
            phase += 55.0 / sr;
            const float v = (float) (0.5 * std::sin (2 * juce::MathConstants<double>::pi * phase)
                                     + 0.2 * (rnd.nextFloat() * 2 - 1));
            for (int c = 0; c < view.getNumChannels(); ++c)
                view.setSample (c, i, v);
        }
        if (fuzzParams)
            for (int k = 0; k < 3; ++k)
            {
                auto* prm = params[rnd.nextInt (params.size())];
                const auto nm = prm->getName (32);
                // latency-changing modes are fuzzed rarely (each switch mutes for the new latency)
                if (nm == "Bypass" || nm.startsWith ("Program") || ((nm == "Phase Mode" || nm == "Quality") && rnd.nextInt (50) != 0))
                    continue;
                if (nm == "Output" || nm == "Mix")
                    prm->setValueNotifyingHost (0.5f + 0.5f * rnd.nextFloat());
                else
                    prm->setValueNotifyingHost (rnd.nextFloat());
            }
        p.processBlock (view, midi);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float s = view.getSample (c, i);
                if (! std::isfinite (s)) return false;
                peakOut = std::max (peakOut, std::abs (s));
            }
        done += n;
    }
    return peakOut < 64.0f;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: EmberHostTest <EMBER.vst3>\n"); return 1; }
    const juce::String path = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName();

    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    juce::Random rnd (1234);

    std::printf ("EMBER host test (VST3): %s\n\n", path.toRawUTF8());

    // 1. Load + metadata
    auto plugin = load (fm, path, 48000.0, 512);
    report (plugin != nullptr, "Plugin loads through the VST3 host");
    if (plugin == nullptr) return 1;
    report (plugin->getName() == "EMBER", "Name", plugin->getName());
    const int numParams = plugin->getParameters().size();
    report (numParams >= 25, "Parameters exposed", juce::String (numParams));
    // VST3 exposes hashed numeric IDs derived from the string IDs: they must be identical across instances.
    auto idsOf = [] (juce::AudioPluginInstance& inst)
    {
        juce::StringArray ids;
        for (auto* prm : inst.getParameters())
            if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*> (prm))
                ids.add (hp->getParameterID() + "=" + prm->getName (32));
        return ids;
    };
    {
        auto other = load (fm, path, 44100.0, 128);
        const auto a = idsOf (*plugin), b = idsOf (*other);
        report (a == b && a.size() == numParams, "Parameter IDs stable across instances", a[5]);
    }
    report (plugin->getBypassParameter() != nullptr, "Host bypass parameter");

    // 2. Sample rates x block sizes
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int block : { 32, 64, 256, 1024, 2048 })
        {
            plugin->releaseResources();
            plugin->setRateAndBufferSizeDetails (sr, block);
            plugin->prepareToPlay (sr, block);
            float peak = 0;
            const bool ok = run (*plugin, sr, block, 0.5, false, false, rnd, peak);
            if (! ok || block == 256)
                report (ok, "Process " + juce::String ((int) sr) + " Hz, block " + juce::String (block),
                        "peak " + juce::String (peak, 3));
        }

    // 3. Random block sizes + parameter fuzzing
    {
        plugin->releaseResources();
        plugin->prepareToPlay (48000.0, 1024);
        float peak = 0;
        const bool ok = run (*plugin, 48000.0, 1024, 6.0, true, true, rnd, peak);   // run before formatting peak
        report (ok && peak > 0.01f, "Random block sizes + parameter fuzzing (6 s)",
                "peak " + juce::String (peak, 3));
    }

    // 4. State round trip
    {
        for (auto* prm : plugin->getParameters())
            if (prm->getName (32) != "Bypass")
                prm->setValueNotifyingHost (rnd.nextFloat());
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        auto second = load (fm, path, 48000.0, 512);
        second->setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        float maxDiff = 0;
        juce::String worst;
        auto a = plugin->getParameters(), b = second->getParameters();
        for (int i = 0; i < a.size(); ++i)
        {
            float d = std::abs (a[i]->getValue() - b[i]->getValue());
            if (a[i]->getName (32).startsWith ("Program"))      // stepped: compare the program index
                d = a[i]->getNumSteps() > 1 && std::abs (d * (a[i]->getNumSteps() - 1)) < 0.5f ? 0.0f : d;
            if (d > maxDiff) { maxDiff = d; worst = a[i]->getName (32) + " " + juce::String (a[i]->getValue(), 3) + " vs " + juce::String (b[i]->getValue(), 3); }
        }
        report (maxDiff < 1.0e-4f, "State save/restore round trip", "max diff " + juce::String (maxDiff, 6) + " " + worst
                + ", " + juce::String ((int) state.getSize()) + " bytes");
    }

    // 5. Latency reporting follows Phase Mode / Quality
    {
        plugin->releaseResources();
        plugin->prepareToPlay (48000.0, 512);
        juce::AudioProcessorParameter* phaseP = nullptr; juce::AudioProcessorParameter* qualP = nullptr;
        for (auto* prm : plugin->getParameters())
        {
            if (prm->getName (32) == "Phase Mode") phaseP = prm;
            if (prm->getName (32) == "Quality") qualP = prm;
        }
        auto latencyAfter = [&] (float phase, float qual)
        {
            phaseP->setValueNotifyingHost (phase);
            qualP->setValueNotifyingHost (qual);
            float peak = 0;
            run (*plugin, 48000.0, 512, 0.1, false, false, rnd, peak);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            return plugin->getLatencySamples();
        };
        const int zl1 = latencyAfter (0.0f, 0.0f), zl2 = latencyAfter (0.0f, 0.5f), lp2 = latencyAfter (1.0f, 0.5f), lp4 = latencyAfter (1.0f, 1.0f);
        report (zl1 == 0 && zl2 > 0 && lp2 > 4000 && lp4 > lp2, "Latency reported to host per mode",
                "ZL 1x " + juce::String (zl1) + ", ZL 2x " + juce::String (zl2) + ", LinPh 2x " + juce::String (lp2) + ", LinPh 4x " + juce::String (lp4));
    }

    // 6. Mono layout
    {
        auto mono = load (fm, path, 48000.0, 256);
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::mono());
        l.outputBuses.add (juce::AudioChannelSet::mono());
        const bool accepted = mono->setBusesLayout (l);
        bool ok = false; float peak = 0;
        if (accepted)
        {
            mono->prepareToPlay (48000.0, 256);
            ok = run (*mono, 48000.0, 256, 1.0, false, true, rnd, peak);
        }
        report (accepted && ok, "Mono in/out layout", "peak " + juce::String (peak, 3));
    }

    // 7. Editor open/close, while audio runs on another thread
    {
        plugin->releaseResources();
        plugin->prepareToPlay (48000.0, 256);
        std::atomic<bool> stop { false }, audioOk { true };
        std::thread audio ([&]
        {
            juce::Random r2 (99);
            while (! stop)
            {
                float peak = 0;
                if (! run (*plugin, 48000.0, 256, 0.05, false, false, r2, peak)) audioOk = false;
            }
        });
        bool editorsOk = true;
        for (int k = 0; k < 4; ++k)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
            editorsOk = editorsOk && ed != nullptr && ed->getWidth() > 0;
            if (ed != nullptr)
            {
                ed->setSize (ed->getWidth() * (k + 3) / 4, ed->getHeight() * (k + 3) / 4);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
                // move parameters from the message thread while audio runs
                for (auto* prm : plugin->getParameters())
                    if (prm->getName (32) != "Bypass")
                        prm->setValueNotifyingHost (rnd.nextFloat());
                juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
            }
        }
        stop = true;
        audio.join();
        report (editorsOk && audioOk, "Editor open/resize/close x4 with audio thread running");
    }

    plugin.reset();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
