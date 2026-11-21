#include "Presets.h"

namespace ember
{
namespace
{
    struct Spec
    {
        const char* name; const char* desc;
        float warmth, drive, character, symmetry, wFreq, boost, atten, glue, toneKHz;
        float subHarm = 0.0f, subLevel = 0.0f, res = 0.30f;
    };

    Preset make (const Spec& s)
    {
        EngineParams e;                       // starts from the defaults
        e.driveDb = s.drive;
        e.character = s.character * 0.01f;
        e.symmetry = s.symmetry * 0.01f;
        e.weightFreq = s.wFreq;
        e.weightBoost = s.boost;
        e.weightAtten = s.atten;
        e.weightRes = s.res;
        e.glueAmount = s.glue * 0.01f;
        e.toneHz = s.toneKHz * 1000.0f;
        e.subHarmonics = s.subHarm * 0.01f;
        e.subLevelDb = s.subLevel;
        e.gainMatch = true;
        e.monoOn = true;
        e.monoFreq = 120.0f;
        return { s.name, s.desc, baseForEffective (e, s.warmth * 0.01f) };
    }
}

const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets = []
    {
        const Spec specs[] = {
            //  name               description                                   W    Drv  Chr  Sym  Hz  Bst Att Glue Tone  SubH SubL Res
            { "Init Warm",        "Any bass. Neutral starting point.",           40,  8,  25,  60,  60, 3,  2,  15,  9 },
            { "Deep Velvet",      "Deep house, round basses.",                   55,  6,  50,  50,  55, 4,  3,  25,  6 },
            { "Tech Grit",        "Tech house basslines with bite.",             60, 12,  80,  75,  70, 2,  3,  20, 12 },
            { "Sub Lifter",       "Near-sine sub that vanishes on a laptop.",    45, 10,  20,  55,  50, 3,  1,  10,  8, 25.0f },
            { "Club Tight",       "Firm low end, clean low-mids.",               35,  5,  40,  70,  60, 4,  4,  30, 10 },
            { "Bass House Heat",  "Bass house, obvious saturation.",             80, 18,  90,  65,  65, 3,  2,  35, 14 },
            { "Rolling Garage",   "Rolling offbeat basses, gentle grit.",        50,  9,  35,  55,  65, 3,  2,  20,  8 },
            { "Tape Pillow",      "Soft, compressed, tape-ish roundness.",       50,  7,  50,  45,  55, 3,  2,  25,  7, 0.0f, 0.0f, 0.20f },
            { "Iron Pressure",    "Dense, firm, forward low-mids.",              65, 14, 100,  70,  60, 3,  3,  30, 10 },
            { "Laptop Rescue",    "Makes a pure sub audible on small speakers.", 50, 10,  30,  50,  60, 2,  2,  15,  9, 55.0f, -1.0f },
            { "Acid Round",       "Resonant synth basslines, tamed top.",        55, 13,  60,  60,  70, 2,  3,  15, 11, 0.0f, 0.0f, 0.40f },
            { "Gentle Glue",      "Barely-there warmth, mostly control.",        30,  4,  25,  80,  60, 2,  1,  45, 12 },
        };
        std::vector<Preset> v;
        for (auto& s : specs)
            v.push_back (make (s));
        return v;
    }();
    return presets;
}

juce::var presetToJson (const juce::String& name, const EngineParams& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "ember-preset");
    o->setProperty ("version", 1);
    o->setProperty ("name", name);
    auto* v = new juce::DynamicObject();
    v->setProperty (pid::warmth, p.warmth * 100.0f);
    v->setProperty (pid::xoverFreq, p.xoverFreq);
    v->setProperty (pid::phaseMode, (int) p.phaseMode);
    v->setProperty (pid::subLevel, p.subLevelDb);
    v->setProperty (pid::subHarmonics, p.subHarmonics * 100.0f);
    v->setProperty (pid::drive, p.driveDb);
    v->setProperty (pid::character, p.character * 100.0f);
    v->setProperty (pid::symmetry, p.symmetry * 100.0f);
    v->setProperty (pid::quality, (int) p.quality);
    v->setProperty (pid::tone, p.toneHz);
    v->setProperty (pid::weightFreq, p.weightFreq);
    v->setProperty (pid::weightBoost, p.weightBoost);
    v->setProperty (pid::weightAtten, p.weightAtten);
    v->setProperty (pid::weightRes, p.weightRes * 100.0f);
    v->setProperty (pid::glueAmount, p.glueAmount * 100.0f);
    v->setProperty (pid::glueAttack, p.glueAttackMs);
    v->setProperty (pid::glueReleaseAuto, p.glueReleaseAuto);
    v->setProperty (pid::glueRelease, p.glueReleaseMs);
    v->setProperty (pid::monoOn, p.monoOn);
    v->setProperty (pid::monoFreq, p.monoFreq);
    v->setProperty (pid::mix, p.mix * 100.0f);
    v->setProperty (pid::gainMatch, p.gainMatch);
    v->setProperty (pid::output, p.outputDb);
    v->setProperty (pid::softClip, p.softClip);
    o->setProperty ("params", juce::var (v));
    return juce::var (o);
}

bool presetFromJson (const juce::var& json, juce::String& name, EngineParams& p)
{
    if (json.getProperty ("format", {}).toString() != "ember-preset")
        return false;
    name = json.getProperty ("name", "Untitled").toString();
    auto v = json.getProperty ("params", {});
    if (! v.isObject())
        return false;

    EngineParams d;   // missing keys fall back to defaults (forward compatible)
    auto num = [&v] (const char* id, float def) { return v.hasProperty (id) ? (float) v[id] : def; };
    auto flag = [&v] (const char* id, bool def) { return v.hasProperty (id) ? (bool) v[id] : def; };

    p.warmth = num (pid::warmth, d.warmth * 100.0f) * 0.01f;
    p.xoverFreq = num (pid::xoverFreq, d.xoverFreq);
    p.phaseMode = num (pid::phaseMode, 0) > 0.5f ? PhaseMode::linearPhase : PhaseMode::zeroLatency;
    p.subLevelDb = num (pid::subLevel, d.subLevelDb);
    p.subHarmonics = num (pid::subHarmonics, 0) * 0.01f;
    p.driveDb = num (pid::drive, d.driveDb);
    p.character = num (pid::character, d.character * 100.0f) * 0.01f;
    p.symmetry = num (pid::symmetry, d.symmetry * 100.0f) * 0.01f;
    p.quality = static_cast<Quality> (juce::jlimit (0, 2, (int) num (pid::quality, 1)));
    p.toneHz = num (pid::tone, d.toneHz);
    p.weightFreq = num (pid::weightFreq, d.weightFreq);
    p.weightBoost = num (pid::weightBoost, d.weightBoost);
    p.weightAtten = num (pid::weightAtten, d.weightAtten);
    p.weightRes = num (pid::weightRes, d.weightRes * 100.0f) * 0.01f;
    p.glueAmount = num (pid::glueAmount, d.glueAmount * 100.0f) * 0.01f;
    p.glueAttackMs = num (pid::glueAttack, d.glueAttackMs);
    p.glueReleaseAuto = flag (pid::glueReleaseAuto, d.glueReleaseAuto);
    p.glueReleaseMs = num (pid::glueRelease, d.glueReleaseMs);
    p.monoOn = flag (pid::monoOn, d.monoOn);
    p.monoFreq = num (pid::monoFreq, d.monoFreq);
    p.mix = num (pid::mix, 100.0f) * 0.01f;
    p.gainMatch = flag (pid::gainMatch, true);
    p.outputDb = num (pid::output, 0.0f);
    p.softClip = flag (pid::softClip, false);
    return true;
}

juce::File getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Ember Audio").getChildFile ("EMBER").getChildFile ("Presets");
}

std::vector<Preset> loadUserPresets()
{
    std::vector<Preset> out;
    auto dir = getUserPresetDirectory();
    if (! dir.isDirectory())
        return out;
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();
    for (auto& f : files)
    {
        Preset pr;
        if (presetFromJson (juce::JSON::parse (f), pr.name, pr.params))
        {
            pr.description = "User preset";
            out.push_back (pr);
        }
    }
    return out;
}

bool saveUserPreset (const juce::String& name, const EngineParams& p)
{
    auto dir = getUserPresetDirectory();
    if (! dir.createDirectory())
        return false;
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty())
        return false;
    return dir.getChildFile (safe + ".json").replaceWithText (juce::JSON::toString (presetToJson (name, p)));
}

} // namespace ember
