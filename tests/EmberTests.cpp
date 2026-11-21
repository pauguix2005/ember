// Offline test runner for the EMBER DSP engine.
// Usage: EmberTests [outputDirForWavs]
//
// Covers the spec's "Prueba 2: técnica" (null test with Mix 0 %, crossover sum,
// no clicks when automating) plus latency, mono-below, soft clip, CPU and
// renders of the factory presets on a sine sub and a synth bassline.

#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/EmberEngine.h"
#include "Presets.h"
#include <chrono>
#include <functional>

using namespace ember;

namespace
{
constexpr double fs = 48000.0;
int failures = 0;

void report (bool ok, const juce::String& name, const juce::String& detail)
{
    if (! ok) ++failures;
    std::printf ("[%s] %-46s %s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.toRawUTF8());
}

/** "Clean" settings: everything that colours the sound is neutralised. */
EngineParams cleanParams()
{
    EngineParams p;
    p.driveDb = 0; p.symmetry = 1.0f; p.character = 0; p.toneHz = 20000;
    p.weightBoost = 0; p.weightAtten = 0; p.glueAmount = 0; p.subHarmonics = 0;
    p.gainMatch = false; p.monoOn = false; p.mix = 1.0f; p.outputDb = 0; p.softClip = false;
    return p;
}

juce::AudioBuffer<float> render (const EngineParams& eff, const juce::AudioBuffer<float>& in,
                                 int block = 256, int* latencyOut = nullptr)
{
    EmberEngine e;
    e.setParameters (eff);
    e.prepare (fs, block, in.getNumChannels());
    e.setParameters (eff);
    if (latencyOut != nullptr) *latencyOut = e.getLatencySamples();

    juce::AudioBuffer<float> out (in);
    for (int s = 0; s < out.getNumSamples(); s += block)
    {
        const int n = std::min (block, out.getNumSamples() - s);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), s, n);
        e.setParameters (eff);
        e.process (view);
    }
    return out;
}

juce::AudioBuffer<float> sine (double f, double ampDb, double seconds, int chans = 2)
{
    const int n = (int) (seconds * fs);
    juce::AudioBuffer<float> b (chans, n);
    const double a = std::pow (10.0, ampDb / 20.0);
    for (int i = 0; i < n; ++i)
    {
        const double fade = std::min (1.0, i / (0.01 * fs));
        const float v = (float) (a * fade * std::sin (2.0 * dsp::pi * f * i / fs));
        for (int c = 0; c < chans; ++c) b.setSample (c, i, v);
    }
    return b;
}

/** Band-limited saw bassline: A1 A1 C2 G1 in 8ths at 124 BPM, plucky envelope.
    Right channel is detuned by 6 cents (unison) to create low-frequency width. */
juce::AudioBuffer<float> bassline (double seconds, bool unisonWidth)
{
    const int n = (int) (seconds * fs);
    juce::AudioBuffer<float> b (2, n);
    const double notes[] = { 55.0, 55.0, 65.406, 48.999 };
    const double eighth = 60.0 / 124.0 / 2.0;
    double ph[2] {};
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const int step = (int) (t / eighth);
        const double local = t - step * eighth;
        const double f = notes[step % 4];
        const double env = std::min (1.0, local / 0.003) * std::exp (-local / 0.18);
        for (int c = 0; c < 2; ++c)
        {
            const double fc = f * (c == 1 && unisonWidth ? std::pow (2.0, 6.0 / 1200.0) : 1.0);
            ph[c] += fc / fs;
            double v = 0.0;
            for (int k = 1; k * fc < 6000.0; ++k)
                v += std::sin (2.0 * dsp::pi * k * ph[c]) / k * std::exp (-k * fc / 2500.0);
            b.setSample (c, i, (float) (0.35 * env * v));
        }
    }
    return b;
}

double rmsDb (const juce::AudioBuffer<float>& b, int ch, int start, int len)
{
    double s = 0;
    for (int i = start; i < start + len; ++i) s += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return 10.0 * std::log10 (s / len + 1e-30);
}

/** Goertzel magnitude (linear amplitude) of frequency f over a window. */
double toneAmp (const juce::AudioBuffer<float>& b, int ch, int start, int len, double f)
{
    double re = 0, im = 0;
    for (int i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * dsp::pi * i / (len - 1));   // Hann
        const double v = b.getSample (ch, start + i) * w;
        re += v * std::cos (2.0 * dsp::pi * f * i / fs);
        im -= v * std::sin (2.0 * dsp::pi * f * i / fs);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (0.5 * len);
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (b.getNumChannels()).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (os, opts))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// ---------------------------------------------------------------------------------

void testNullMixZero()
{
    for (auto phase : { PhaseMode::zeroLatency, PhaseMode::linearPhase })
        for (auto q : { Quality::eco, Quality::normal, Quality::hq })
        {
            auto p = cleanParams();
            p.driveDb = 12; p.character = 0.5f; p.weightBoost = 6; p.glueAmount = 0.5f;   // wet is heavily coloured
            p.mix = 0.0f; p.phaseMode = phase; p.quality = q;
            auto in = bassline (3.0, true);
            int lat = 0;
            auto out = render (p, in, 256, &lat);

            // Reference: what the dry path is defined to be — the input through the
            // crossover's all-pass (zero latency) or a pure delay (linear phase).
            juce::AudioBuffer<float> ref (in);
            if (phase == PhaseMode::zeroLatency)
            {
                dsp::SvfCoeffs c; c.set (120.0, fs);
                for (int ch = 0; ch < 2; ++ch)
                {
                    dsp::AllPass2 ap;
                    for (int i = 0; i < ref.getNumSamples(); ++i)
                        ref.setSample (ch, i, (float) ap.process (c, in.getSample (ch, i)));
                }
            }
            double err = 0, sig = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = lat + 4800; i < out.getNumSamples(); ++i)
                {
                    const double d = out.getSample (ch, i) - ref.getSample (ch, i - lat);
                    err += d * d; sig += (double) ref.getSample (ch, i - lat) * ref.getSample (ch, i - lat);
                }
            const double nullDb = 10 * std::log10 (err / sig + 1e-30);
            report (nullDb < -90.0,
                    juce::String ("Null, Mix 0 % (") + (phase == PhaseMode::zeroLatency ? "ZL" : "LinPh")
                        + ", " + juce::String (q == Quality::eco ? "1x" : q == Quality::normal ? "2x" : "4x") + ")",
                    juce::String (nullDb, 1) + " dB residual, latency " + juce::String (lat));
        }
}

void testCrossoverSum()
{
    // Low-level impulse: the saturator is in its linear region, so the wet path
    // should be SUB + BODY = flat magnitude (all-pass or pure delay).
    for (auto phase : { PhaseMode::zeroLatency, PhaseMode::linearPhase })
        for (auto q : { Quality::eco, Quality::normal, Quality::hq })
        {
            auto p = cleanParams();
            p.phaseMode = phase; p.quality = q;
            const int n = 65536;
            juce::AudioBuffer<float> in (1, n);
            in.clear();
            in.setSample (0, 1000, 1.0e-3f);
            int lat = 0;
            auto out = render (p, in, 256, &lat);

            juce::dsp::FFT fft (16);
            std::vector<float> data (2 * (size_t) n, 0.0f);
            for (int i = 0; i < n; ++i) data[(size_t) i] = out.getSample (0, i) / 1.0e-3f;
            fft.performFrequencyOnlyForwardTransform (data.data());
            double worst = 0, worstF = 0;
            for (int k = 1; k < n / 2; ++k)
            {
                const double f = k * fs / n;
                if (f < 20.0 || f > 5000.0) continue;
                const double db = 20 * std::log10 (data[(size_t) k] + 1e-12);
                if (std::abs (db) > std::abs (worst)) { worst = db; worstF = f; }
            }
            report (std::abs (worst) < 0.5,
                    juce::String ("Crossover sum flat 20 Hz-5 kHz (") + (phase == PhaseMode::zeroLatency ? "ZL" : "LinPh")
                        + ", " + juce::String (q == Quality::eco ? "1x" : q == Quality::normal ? "2x" : "4x") + ")",
                    "worst " + juce::String (worst, 3) + " dB at " + juce::String (worstF, 0) + " Hz");
        }
}

void testLatency()
{
    for (auto phase : { PhaseMode::zeroLatency, PhaseMode::linearPhase })
        for (auto q : { Quality::eco, Quality::normal, Quality::hq })
        {
            auto p = cleanParams();
            p.phaseMode = phase; p.quality = q;
            juce::AudioBuffer<float> in (1, 48000);
            in.clear();
            // band-limited click centred at 2000 (mid band, away from the split)
            for (int i = -64; i <= 64; ++i)
            {
                const double x = i * 0.25;
                const double s = i == 0 ? 1.0 : std::sin (dsp::pi * x) / (dsp::pi * x);
                in.setSample (0, 2000 + i, (float) (1e-3 * s * (0.5 + 0.5 * std::cos (dsp::pi * i / 64.0))));
            }
            p.mix = 1.0f;
            int lat = 0;
            auto out = render (p, in, 256, &lat);
            int peak = 0; float mx = 0;
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (std::abs (out.getSample (0, i)) > mx) { mx = std::abs (out.getSample (0, i)); peak = i; }
            const int measured = peak - 2000;
            report (std::abs (measured - lat) <= 1,
                    juce::String ("Reported latency (") + (phase == PhaseMode::zeroLatency ? "ZL" : "LinPh") + ", "
                        + juce::String (q == Quality::eco ? "1x" : q == Quality::normal ? "2x" : "4x") + ")",
                    "reported " + juce::String (lat) + ", measured " + juce::String (measured));
        }
}

struct HarmonicStats { double fundDb, harmDb; };

HarmonicStats harmonicsAbove (const juce::AudioBuffer<float>& b, double f0, double above)
{
    const int start = (int) (1.0 * fs), len = (int) (1.0 * fs);
    const double a1 = toneAmp (b, 0, start, len, f0);
    double hs = 0;
    for (int k = 2; k * f0 < 1000.0; ++k)
        if (k * f0 > above)
        {
            const double a = toneAmp (b, 0, start, len, k * f0);
            hs += a * a;
        }
    return { 20 * std::log10 (a1 + 1e-12), 10 * std::log10 (hs + 1e-24) };
}

void testSubHarmonics (const juce::File& outDir)
{
    // The spec's Problem #1: a near-sine sub has nothing above 120 Hz, so
    // saturating BODY alone adds nothing audible on small speakers.
    auto in = sine (55.0, -6.0, 3.0);
    auto base = harmonicsAbove (in, 55, 150);
    std::printf ("\n  55 Hz sine at -6 dBFS — harmonic energy 150 Hz-1 kHz relative to the fundamental:\n");
    std::printf ("    %-34s %7.1f dB\n", "input", base.harmDb - base.fundDb);

    for (auto& pr : getFactoryPresets())
    {
        auto eff = applyWarmthMacro (pr.params);
        eff.gainMatch = false;
        auto out = render (eff, in);
        auto h = harmonicsAbove (out, 55, 150);
        std::printf ("    %-34s %7.1f dB   (sub harmonics %3.0f %%)\n", pr.name.toRawUTF8(), h.harmDb - h.fundDb, eff.subHarmonics * 100.0);
        if (outDir.isDirectory())
            writeWav (outDir.getChildFile ("sine55_" + pr.name.replaceCharacter (' ', '_') + ".wav"), out);
    }

    auto p = applyWarmthMacro (getFactoryPresets()[0].params);
    p.gainMatch = false;
    p.glueAmount = 0.0f;          // isolate the generator from the compressor
    p.subHarmonics = 0.0f;
    auto off = harmonicsAbove (render (p, in), 55, 150);
    p.subHarmonics = 1.0f;
    auto on = harmonicsAbove (render (p, in), 55, 150);
    report (on.harmDb - on.fundDb > -14.0 && on.harmDb - off.harmDb > 15.0,
            "Sub Harmonics makes a sine sub audible",
            "100 %: " + juce::String (on.harmDb - on.fundDb, 1) + " dB re fundamental (0 %: "
                + juce::String (off.harmDb - off.fundDb, 1) + " dB)");
    report (std::abs (on.fundDb - off.fundDb) < 0.5, "Sub Harmonics leaves the fundamental intact",
            "fundamental change " + juce::String (on.fundDb - off.fundDb, 2) + " dB");
}

void testMonoBelow()
{
    auto in = bassline (3.0, true);
    auto p = cleanParams();
    p.monoOn = true; p.monoFreq = 120;
    auto out = render (p, in);
    auto sideLow = [] (const juce::AudioBuffer<float>& b)
    {
        juce::AudioBuffer<float> s (1, b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i) s.setSample (0, i, 0.5f * (b.getSample (0, i) - b.getSample (1, i)));
        double e = 0;
        for (double f : { 49.0, 55.0, 65.406 })
        {
            double a = toneAmp (s, 0, (int) fs, (int) fs, f);
            e += a * a;
        }
        return 10 * std::log10 (e + 1e-24);
    };
    const double before = sideLow (in), after = sideLow (out);
    report (after - before < -24.0, "Mono Below 120 Hz removes low side energy",
            "side at fundamentals: " + juce::String (before, 1) + " -> " + juce::String (after, 1) + " dB");
}

void testSoftClip()
{
    auto p = cleanParams();
    p.softClip = true; p.outputDb = 12; p.mix = 0;
    auto out = render (p, sine (200, 0.0, 1.0));
    const float peak = out.getMagnitude (0, out.getNumSamples());
    report (peak <= 0.9661f, "Soft Clip ceiling -0.3 dBFS", "peak " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS");
}

void testAutomationClicks()
{
    // Jump one control at a time between its extremes every 50 ms on a 55 Hz sine.
    // Click detector: peak of the output high-passed at 4 kHz (8th order). A click
    // is broadband; a smooth 20 ms parameter ramp is not. It must stay within 2x
    // (+6 dB) of the worse of the two static settings.
    auto in = sine (55.0, -10.0, 2.0);
    const auto base = applyWarmthMacro (getFactoryPresets()[0].params);

    struct Move { const char* name; std::function<void (EngineParams&)> set; };
    const std::vector<Move> moves = {
        { "drive",     [] (EngineParams& q) { q.driveDb = 24; } },
        { "character", [] (EngineParams& q) { q.character = 1; } },
        { "symmetry",  [] (EngineParams& q) { q.symmetry = 0; } },
        { "crossover", [] (EngineParams& q) { q.xoverFreq = 250; } },
        { "tone",      [] (EngineParams& q) { q.toneHz = 2000; } },
        { "weight",    [] (EngineParams& q) { q.weightBoost = 12; q.weightAtten = 12; q.weightFreq = 150; q.weightRes = 1; } },
        { "glue",      [] (EngineParams& q) { q.glueAmount = 1; q.glueAttackMs = 5; } },
        { "mix",       [] (EngineParams& q) { q.mix = 0.0f; } },
        { "output",    [] (EngineParams& q) { q.outputDb = -12; } },
        { "sub",       [] (EngineParams& q) { q.subLevelDb = 6; q.subHarmonics = 1; } },
        { "mono",      [] (EngineParams& q) { q.monoFreq = 200; q.monoOn = false; } },
        { "gainMatch", [] (EngineParams& q) { q.gainMatch = false; } },
    };
    auto maxD2 = [] (const juce::AudioBuffer<float>& b)
    {
        dsp::Biquad hp[4];
        for (auto& f : hp) f.setHighPass (fs, 4000.0, 0.7071);
        double m = 1.0e-9;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            double v = b.getSample (0, i);
            for (auto& f : hp) v = f.process (v);
            if (i > 4800) m = std::max (m, std::abs (v));
        }
        return m;
    };

    double worst = 0; juce::String worstName;
    for (auto& mv : moves)
    {
        auto alt = base;
        mv.set (alt);
        EmberEngine e;
        e.setParameters (base);
        e.prepare (fs, 128, 2);
        juce::AudioBuffer<float> out (in);
        for (int s = 0; s < out.getNumSamples(); s += 128)
        {
            e.setParameters ((s / 2400) % 2 == 1 ? alt : base);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, s, std::min (128, out.getNumSamples() - s));
            e.process (view);
        }
        const double moving = maxD2 (out), still = std::max (maxD2 (render (base, in, 128)), maxD2 (render (alt, in, 128)));
        std::printf ("       %-10s HF peak moving %6.1f dBFS, static %6.1f dBFS\n", mv.name,
                     juce::Decibels::gainToDecibels (moving, -200.0), juce::Decibels::gainToDecibels (still, -200.0));
        // Pass if within +6 dB of the static case or at least 60 dB below the -10 dBFS test tone.
        const double ratio = moving / std::max (still, std::pow (10.0, -70.0 / 20.0));
        if (ratio > worst) { worst = ratio; worstName = mv.name; }
        if (ratio > 2.0)
            std::printf ("       automation of %-10s ratio %.2f\n", mv.name, ratio);
    }
    report (worst < 2.0, "No clicks when automating each parameter",
            "worst HF-peak ratio " + juce::String (worst, 2) + " (" + worstName + ")");
}

void testModeSwitch()
{
    auto in = bassline (3.0, false);
    auto p = applyWarmthMacro (getFactoryPresets()[0].params);
    EmberEngine e;
    e.setParameters (p);
    e.prepare (fs, 256, 2);
    juce::AudioBuffer<float> out (in);
    bool finite = true;
    for (int s = 0, k = 0; s < out.getNumSamples(); s += 256, ++k)
    {
        auto q = p;
        q.quality = (Quality) ((k / 40) % 3);
        q.phaseMode = (k / 60) % 2 ? PhaseMode::linearPhase : PhaseMode::zeroLatency;
        e.setParameters (q);
        if (k % 30 == 0) e.updateLinearPhaseFilter();
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, s, std::min (256, out.getNumSamples() - s));
        e.process (view);
    }
    for (int i = 0; i < out.getNumSamples(); ++i)
        finite = finite && std::isfinite (out.getSample (0, i));
    const float peak = out.getMagnitude (0, out.getNumSamples());
    report (finite && peak < 2.0f * in.getMagnitude (0, in.getNumSamples()), "Phase/quality switching is safe",
            "peak " + juce::String (peak, 3));
}

void testFuzzNeverStuck()
{
    // Random parameters every block (latency modes rarely), random block sizes.
    // The output must stay finite and never go silent for more than 0.5 s while
    // the input plays and Mix/Output are at least half way.
    int worstSilent = 0; bool finite = true;
    for (int seed = 1; seed <= 6; ++seed)
    {
        juce::Random rnd (seed);
        EmberEngine e;
        EngineParams p;
        e.setParameters (p);
        e.prepare (fs, 1024, 2);
        auto in = bassline (6.0, true);
        int silentRun = 0;
        for (int s = 0; s < in.getNumSamples();)
        {
            const int n = std::min (1 + rnd.nextInt (1024), in.getNumSamples() - s);
            p.driveDb = rnd.nextFloat() * 24; p.character = rnd.nextFloat(); p.symmetry = rnd.nextFloat();
            p.xoverFreq = 60 + rnd.nextFloat() * 190; p.toneHz = 2000 + rnd.nextFloat() * 18000;
            p.weightBoost = rnd.nextFloat() * 12; p.weightAtten = rnd.nextFloat() * 12; p.weightFreq = 20 + rnd.nextFloat() * 130;
            p.glueAmount = rnd.nextFloat(); p.subHarmonics = rnd.nextFloat(); p.subLevelDb = -12 + rnd.nextFloat() * 18;
            p.mix = 0.5f + 0.5f * rnd.nextFloat(); p.outputDb = -6 + rnd.nextFloat() * 12;
            p.monoOn = rnd.nextBool(); p.gainMatch = rnd.nextBool(); p.softClip = rnd.nextBool();
            if (rnd.nextInt (60) == 0) p.quality = (Quality) rnd.nextInt (3);
            if (rnd.nextInt (120) == 0) p.phaseMode = rnd.nextBool() ? PhaseMode::linearPhase : PhaseMode::zeroLatency;
            e.setParameters (p);
            if (rnd.nextInt (10) == 0) e.updateLinearPhaseFilter();
            juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, s, n);
            e.process (view);
            const float pk = view.getMagnitude (0, n);
            finite = finite && std::isfinite (pk);
            silentRun = pk < 1.0e-4f ? silentRun + n : 0;
            worstSilent = std::max (worstSilent, silentRun);
            s += n;
        }
    }
    report (finite && worstSilent < (int) (0.5 * fs), "Fuzz: finite and never stuck silent",
            "longest silence " + juce::String (worstSilent / fs * 1000.0, 0) + " ms");
}

void testSilence()
{
    auto p = applyWarmthMacro (getFactoryPresets()[5].params);
    juce::AudioBuffer<float> in (2, 96000);
    in.clear();
    auto out = render (p, in);
    const float peak = out.getMagnitude (0, out.getNumSamples());
    report (peak < 1.0e-6f, "Silence in, silence out", "peak " + juce::String (peak));
}

void testCpu()
{
    auto in = bassline (10.0, true);
    struct Case { const char* name; Quality q; PhaseMode ph; };
    for (auto c : { Case { "Normal 2x, Zero Latency", Quality::normal, PhaseMode::zeroLatency },
                    Case { "HQ 4x, Zero Latency", Quality::hq, PhaseMode::zeroLatency },
                    Case { "Normal 2x, Linear Phase", Quality::normal, PhaseMode::linearPhase } })
    {
        auto p = applyWarmthMacro (getFactoryPresets()[0].params);
        p.quality = c.q; p.phaseMode = c.ph; p.subHarmonics = 0.3f;
        double sec = 1.0e9;   // best of 3: shared cloud CPUs are noisy
        for (int run = 0; run < 3; ++run)
        {
            const auto t0 = std::chrono::steady_clock::now();
            auto out = render (p, in, 256);
            sec = std::min (sec, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        const double pct = 100.0 * sec / 10.0;
        const bool target = c.q == Quality::normal && c.ph == PhaseMode::zeroLatency;
        report (! target || pct < 2.0, juce::String ("CPU: ") + c.name,
                juce::String (pct, 2) + " % of one core (48 kHz, 256, stereo)" + (target ? "  [spec target < 2 %]" : ""));
    }
}

void renderPresets (const juce::File& outDir)
{
    if (! outDir.isDirectory()) return;
    auto in = bassline (8.0, true);
    writeWav (outDir.getChildFile ("bassline_dry.wav"), in);
    writeWav (outDir.getChildFile ("sine55_dry.wav"), sine (55.0, -6.0, 3.0));
    std::printf ("\n  Bassline renders (Gain Match on) — RMS change and harmonics 150 Hz-1 kHz:\n");
    for (auto& pr : getFactoryPresets())
    {
        auto eff = applyWarmthMacro (pr.params);
        auto out = render (eff, in);
        writeWav (outDir.getChildFile ("bassline_" + pr.name.replaceCharacter (' ', '_') + ".wav"), out);
        const int st = (int) (4 * fs), len = (int) (4 * fs);
        std::printf ("    %-18s RMS %+5.1f dB\n", pr.name.toRawUTF8(), rmsDb (out, 0, st, len) - rmsDb (in, 0, st, len));
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;   // message manager for the convolution's loader thread
    juce::File outDir;
    if (argc > 1)
    {
        outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
        outDir.createDirectory();
    }

    std::printf ("EMBER DSP tests\n\n");
    testNullMixZero();
    testCrossoverSum();
    testLatency();
    testMonoBelow();
    testSoftClip();
    testAutomationClicks();
    testModeSwitch();
    testFuzzNeverStuck();
    testSilence();
    testSubHarmonics (outDir);
    std::printf ("\n");
    testCpu();
    renderPresets (outDir);

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
