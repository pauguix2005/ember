// Renders the EMBER editor to a PNG while a test bassline plays through the processor.
// Usage: EmberSnapshot <out.png> [preset name] [simple|advanced] [scale]
#include "PluginProcessor.h"

using namespace ember;

static void fillBassline (juce::AudioBuffer<float>& b, double fs, int64_t& sampleClock)
{
    const double notes[] = { 55.0, 55.0, 65.406, 48.999 };
    const double eighth = 60.0 / 124.0 / 2.0;
    for (int i = 0; i < b.getNumSamples(); ++i, ++sampleClock)
    {
        const double t = sampleClock / fs;
        const int step = (int) (t / eighth);
        const double local = t - step * eighth;
        const double f = notes[step % 4];
        const double env = std::min (1.0, local / 0.003) * std::exp (-local / 0.25);
        for (int c = 0; c < 2; ++c)
        {
            const double fc = f * (c == 1 ? 1.0035 : 1.0);
            double v = 0.0;
            for (int k = 1; k * fc < 4000.0; ++k)
                v += std::sin (2.0 * juce::MathConstants<double>::pi * k * fc * t) / k * std::exp (-k * fc / 900.0);
            b.setSample (c, i, (float) (0.4 * env * v));
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: EmberSnapshot out.png [preset] [simple|advanced] [scale]\n"); return 1; }
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const juce::String presetName = argc > 2 ? juce::String::fromUTF8 (argv[2]) : "Init Warm";
    const bool advanced = argc > 3 ? juce::String (argv[3]) == "advanced" : true;
    const float scale = argc > 4 ? juce::String (argv[4]).getFloatValue() : 1.0f;

    const double fs = 48000.0;
    const int block = 512;
    EmberProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, block);
    proc.prepareToPlay (fs, block);
    for (auto& p : proc.getAllPresets())
        if (p.name == presetName) proc.loadPreset (p);
    proc.setAdvancedMode (advanced);
    proc.setUiScale (1.0f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditorIfNeeded());
    editor->setSize (900, 520);

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    int64_t clock = 0;
    for (int round = 0; round < 90; ++round)       // ~3 s of audio, interleaved with UI timers
    {
        for (int k = 0; k < 3; ++k)
        {
            fillBassline (buf, fs, clock);
            proc.processBlock (buf, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    juce::PNGImageFormat png;
    out.deleteFile();
    juce::FileOutputStream os (out);
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8());
    editor.reset();
    return ok ? 0 : 1;
}
