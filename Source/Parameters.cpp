#include "Parameters.h"

namespace ember
{
namespace
{
    constexpr float driveSpan  = 20.0f;  // dB of extra drive from 40 % to 100 % ≈ +12 dB
    constexpr float charSpan   = 0.50f;
    constexpr float boostSpan  = 5.0f;
    constexpr float attenSpan  = 4.0f;
    constexpr float glueSpan   = 0.40f;
    constexpr float toneOctPer = 1.0f;   // octaves of Tone per unit of k

    juce::String pct (float v, int)   { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String db (float v, int)    { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String hz (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }
    juce::String ms (float v, int)    { return juce::String (juce::roundToInt (v)) + " ms"; }

    float parseNumber (const juce::String& t)
    {
        auto s = t.trim().toLowerCase();
        auto v = s.retainCharacters ("-+0123456789.,").replaceCharacter (',', '.').getFloatValue();
        if (s.contains ("k"))
            v *= 1000.0f;
        return v;
    }

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }

    using APF = juce::AudioParameterFloat;
    using APC = juce::AudioParameterChoice;
    using APB = juce::AudioParameterBool;
    using Attr = juce::AudioParameterFloatAttributes;

    std::unique_ptr<APF> makeFloat (const char* id, const char* name, juce::NormalisableRange<float> r,
                                    float def, juce::String (*fmt) (float, int), const char* label = "")
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def,
                                      Attr().withLabel (label)
                                            .withStringFromValueFunction (fmt)
                                            .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); }));
    }
}

EngineParams applyWarmthMacro (const EngineParams& b)
{
    auto e = b;
    const float k = b.warmth - warmthNeutral;
    e.driveDb     = juce::jlimit (0.0f, 24.0f, b.driveDb + driveSpan * k);
    e.character   = juce::jlimit (0.0f, 1.0f, b.character + charSpan * k);
    e.weightBoost = juce::jlimit (0.0f, 12.0f, b.weightBoost + boostSpan * k);
    e.weightAtten = juce::jlimit (0.0f, 12.0f, b.weightAtten + attenSpan * k);
    e.glueAmount  = juce::jlimit (0.0f, 1.0f, b.glueAmount + glueSpan * k);
    e.toneHz      = juce::jlimit (2000.0f, 20000.0f, b.toneHz * std::pow (2.0f, -toneOctPer * k));
    return e;
}

EngineParams baseForEffective (const EngineParams& eff, float warmth)
{
    auto b = eff;
    b.warmth = warmth;
    const float k = warmth - warmthNeutral;
    b.driveDb     = juce::jlimit (0.0f, 24.0f, eff.driveDb - driveSpan * k);
    b.character   = juce::jlimit (0.0f, 1.0f, eff.character - charSpan * k);
    b.weightBoost = juce::jlimit (0.0f, 12.0f, eff.weightBoost - boostSpan * k);
    b.weightAtten = juce::jlimit (0.0f, 12.0f, eff.weightAtten - attenSpan * k);
    b.glueAmount  = juce::jlimit (0.0f, 1.0f, eff.glueAmount - glueSpan * k);
    b.toneHz      = juce::jlimit (2000.0f, 20000.0f, eff.toneHz * std::pow (2.0f, toneOctPer * k));
    return b;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto pctRange = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);

    l.add (makeFloat (pid::warmth, "Warmth", pctRange, 40.0f, pct, "%"));

    l.add (makeFloat (pid::xoverFreq, "Crossover", logRange (60.0f, 250.0f), 120.0f, hz, "Hz"));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::phaseMode, 1 }, "Phase Mode",
                                  juce::StringArray { "Zero Latency", "Linear Phase" }, 0));
    l.add (makeFloat (pid::subLevel, "Sub Level", juce::NormalisableRange<float> (-12.0f, 6.0f, 0.01f), 0.0f, db, "dB"));
    l.add (makeFloat (pid::subHarmonics, "Sub Harmonics", pctRange, 0.0f, pct, "%"));

    l.add (makeFloat (pid::drive, "Drive", juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 8.0f, db, "dB"));
    l.add (makeFloat (pid::character, "Character", pctRange, 25.0f, pct, "%"));
    l.add (makeFloat (pid::symmetry, "Symmetry", pctRange, 60.0f, pct, "%"));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::quality, 1 }, "Quality",
                                  juce::StringArray { "Eco (1x)", "Normal (2x)", "HQ (4x)" }, 1));
    l.add (makeFloat (pid::tone, "Tone", logRange (2000.0f, 20000.0f), 9000.0f, hz, "Hz"));

    l.add (makeFloat (pid::weightFreq, "Weight Freq", logRange (20.0f, 150.0f), 60.0f, hz, "Hz"));
    l.add (makeFloat (pid::weightBoost, "Weight Boost", juce::NormalisableRange<float> (0.0f, 12.0f, 0.01f), 3.0f, db, "dB"));
    l.add (makeFloat (pid::weightAtten, "Weight Atten", juce::NormalisableRange<float> (0.0f, 12.0f, 0.01f), 2.0f, db, "dB"));
    l.add (makeFloat (pid::weightRes, "Weight Resonance", pctRange, 30.0f, pct, "%"));

    l.add (makeFloat (pid::glueAmount, "Glue", pctRange, 15.0f, pct, "%"));
    l.add (makeFloat (pid::glueAttack, "Glue Attack", logRange (5.0f, 60.0f), 25.0f, ms, "ms"));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::glueReleaseAuto, 1 }, "Glue Release Auto", true));
    l.add (makeFloat (pid::glueRelease, "Glue Release", logRange (50.0f, 400.0f), 150.0f, ms, "ms"));

    l.add (std::make_unique<APB> (juce::ParameterID { pid::monoOn, 1 }, "Mono Below On", true));
    l.add (makeFloat (pid::monoFreq, "Mono Below", logRange (60.0f, 200.0f), 120.0f, hz, "Hz"));
    l.add (makeFloat (pid::mix, "Mix", pctRange, 100.0f, pct, "%"));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::gainMatch, 1 }, "Gain Match", true));
    l.add (makeFloat (pid::output, "Output", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, db, "dB"));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::softClip, 1 }, "Soft Clip", false));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));
    return l;
}

ParameterRefs::ParameterRefs (juce::AudioProcessorValueTreeState& s)
{
    auto get = [&s] (const char* id)
    {
        auto* p = s.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };
    warmth = get (pid::warmth); xoverFreq = get (pid::xoverFreq); phaseMode = get (pid::phaseMode);
    subLevel = get (pid::subLevel); subHarmonics = get (pid::subHarmonics); drive = get (pid::drive);
    character = get (pid::character); symmetry = get (pid::symmetry); quality = get (pid::quality);
    tone = get (pid::tone); weightFreq = get (pid::weightFreq); weightBoost = get (pid::weightBoost);
    weightAtten = get (pid::weightAtten); weightRes = get (pid::weightRes); glueAmount = get (pid::glueAmount);
    glueAttack = get (pid::glueAttack); glueReleaseAuto = get (pid::glueReleaseAuto); glueRelease = get (pid::glueRelease);
    monoOn = get (pid::monoOn); monoFreq = get (pid::monoFreq); mix = get (pid::mix);
    gainMatch = get (pid::gainMatch); output = get (pid::output); softClip = get (pid::softClip);
    bypass = get (pid::bypass);
}

EngineParams ParameterRefs::read() const
{
    EngineParams p;
    p.warmth       = warmth->load() * 0.01f;
    p.xoverFreq    = xoverFreq->load();
    p.phaseMode    = phaseMode->load() > 0.5f ? PhaseMode::linearPhase : PhaseMode::zeroLatency;
    p.subLevelDb   = subLevel->load();
    p.subHarmonics = subHarmonics->load() * 0.01f;
    p.driveDb      = drive->load();
    p.character    = character->load() * 0.01f;
    p.symmetry     = symmetry->load() * 0.01f;
    p.quality      = static_cast<Quality> (juce::jlimit (0, 2, juce::roundToInt (quality->load())));
    p.toneHz       = tone->load();
    p.weightFreq   = weightFreq->load();
    p.weightBoost  = weightBoost->load();
    p.weightAtten  = weightAtten->load();
    p.weightRes    = weightRes->load() * 0.01f;
    p.glueAmount   = glueAmount->load() * 0.01f;
    p.glueAttackMs = glueAttack->load();
    p.glueReleaseAuto = glueReleaseAuto->load() > 0.5f;
    p.glueReleaseMs = glueRelease->load();
    p.monoOn       = monoOn->load() > 0.5f;
    p.monoFreq     = monoFreq->load();
    p.mix          = mix->load() * 0.01f;
    p.gainMatch    = gainMatch->load() > 0.5f;
    p.outputDb     = output->load();
    p.softClip     = softClip->load() > 0.5f;
    return p;
}

void writeParams (juce::AudioProcessorValueTreeState& s, const EngineParams& p)
{
    auto set = [&s] (const char* id, float plainValue)
    {
        if (auto* param = s.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    };
    set (pid::warmth, p.warmth * 100.0f);
    set (pid::xoverFreq, p.xoverFreq);
    set (pid::phaseMode, p.phaseMode == PhaseMode::linearPhase ? 1.0f : 0.0f);
    set (pid::subLevel, p.subLevelDb);
    set (pid::subHarmonics, p.subHarmonics * 100.0f);
    set (pid::drive, p.driveDb);
    set (pid::character, p.character * 100.0f);
    set (pid::symmetry, p.symmetry * 100.0f);
    set (pid::quality, (float) static_cast<int> (p.quality));
    set (pid::tone, p.toneHz);
    set (pid::weightFreq, p.weightFreq);
    set (pid::weightBoost, p.weightBoost);
    set (pid::weightAtten, p.weightAtten);
    set (pid::weightRes, p.weightRes * 100.0f);
    set (pid::glueAmount, p.glueAmount * 100.0f);
    set (pid::glueAttack, p.glueAttackMs);
    set (pid::glueReleaseAuto, p.glueReleaseAuto ? 1.0f : 0.0f);
    set (pid::glueRelease, p.glueReleaseMs);
    set (pid::monoOn, p.monoOn ? 1.0f : 0.0f);
    set (pid::monoFreq, p.monoFreq);
    set (pid::mix, p.mix * 100.0f);
    set (pid::gainMatch, p.gainMatch ? 1.0f : 0.0f);
    set (pid::output, p.outputDb);
    set (pid::softClip, p.softClip ? 1.0f : 0.0f);
}

} // namespace ember
