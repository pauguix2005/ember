#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ember
{
// Parameter IDs are part of the saved-project format: never rename or reuse them.
namespace pid
{
    inline constexpr const char* warmth          = "warmth";
    inline constexpr const char* xoverFreq       = "xoverFreq";
    inline constexpr const char* phaseMode       = "phaseMode";
    inline constexpr const char* subLevel        = "subLevel";
    inline constexpr const char* subHarmonics    = "subHarmonics";
    inline constexpr const char* drive           = "drive";
    inline constexpr const char* character       = "character";
    inline constexpr const char* symmetry        = "symmetry";
    inline constexpr const char* quality         = "quality";
    inline constexpr const char* tone            = "tone";
    inline constexpr const char* weightFreq      = "weightFreq";
    inline constexpr const char* weightBoost     = "weightBoost";
    inline constexpr const char* weightAtten     = "weightAtten";
    inline constexpr const char* weightRes       = "weightRes";
    inline constexpr const char* glueAmount      = "glueAmount";
    inline constexpr const char* glueAttack      = "glueAttack";
    inline constexpr const char* glueReleaseAuto = "glueReleaseAuto";
    inline constexpr const char* glueRelease     = "glueRelease";
    inline constexpr const char* monoOn          = "monoOn";
    inline constexpr const char* monoFreq        = "monoFreq";
    inline constexpr const char* mix             = "mix";
    inline constexpr const char* gainMatch       = "gainMatch";
    inline constexpr const char* output          = "output";
    inline constexpr const char* softClip        = "softClip";
    inline constexpr const char* bypass          = "bypass";
}

enum class PhaseMode { zeroLatency = 0, linearPhase = 1 };
enum class Quality   { eco = 0, normal = 1, hq = 2 };   // 1x, 2x, 4x oversampling on BODY

/** Plain snapshot of every DSP parameter, in natural units. */
struct EngineParams
{
    float warmth       = 0.40f;   // 0..1
    float xoverFreq    = 120.0f;  // Hz
    PhaseMode phaseMode = PhaseMode::zeroLatency;
    float subLevelDb   = 0.0f;
    float subHarmonics = 0.0f;    // 0..1
    float driveDb      = 8.0f;
    float character    = 0.25f;   // 0..1
    float symmetry     = 0.60f;   // 0..1
    Quality quality    = Quality::normal;
    float toneHz       = 9000.0f;
    float weightFreq   = 60.0f;
    float weightBoost  = 3.0f;
    float weightAtten  = 2.0f;
    float weightRes    = 0.30f;   // 0..1
    float glueAmount   = 0.15f;   // 0..1
    float glueAttackMs = 25.0f;
    bool  glueReleaseAuto = true;
    float glueReleaseMs = 150.0f;
    bool  monoOn       = true;
    float monoFreq     = 120.0f;
    float mix          = 1.0f;    // 0..1
    bool  gainMatch    = true;
    float outputDb     = 0.0f;
    bool  softClip     = false;
};

/** WARMTH is a relative macro: 40 % is neutral (the knobs sound exactly as set).
    Above 40 % it pushes Drive, Character, Boost, Atten and Glue up and Tone down;
    below 40 % it pulls them back. The individual knobs keep their own values,
    so the macro never fights automation of the other parameters. */
inline constexpr float warmthNeutral = 0.40f;

EngineParams applyWarmthMacro (const EngineParams& base);

/** Inverse of the macro for one preset: returns the base knob values that give
    the requested effective sound at the given warmth (used to author presets). */
EngineParams baseForEffective (const EngineParams& effective, float warmth);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Cached raw pointers to the APVTS atomics, for lock-free reading on the audio thread. */
struct ParameterRefs
{
    explicit ParameterRefs (juce::AudioProcessorValueTreeState& state);
    EngineParams read() const;

    std::atomic<float>* warmth; std::atomic<float>* xoverFreq; std::atomic<float>* phaseMode;
    std::atomic<float>* subLevel; std::atomic<float>* subHarmonics; std::atomic<float>* drive;
    std::atomic<float>* character; std::atomic<float>* symmetry; std::atomic<float>* quality;
    std::atomic<float>* tone; std::atomic<float>* weightFreq; std::atomic<float>* weightBoost;
    std::atomic<float>* weightAtten; std::atomic<float>* weightRes; std::atomic<float>* glueAmount;
    std::atomic<float>* glueAttack; std::atomic<float>* glueReleaseAuto; std::atomic<float>* glueRelease;
    std::atomic<float>* monoOn; std::atomic<float>* monoFreq; std::atomic<float>* mix;
    std::atomic<float>* gainMatch; std::atomic<float>* output; std::atomic<float>* softClip;
    std::atomic<float>* bypass;
};

/** Writes an EngineParams snapshot into the APVTS (message thread, notifies host). */
void writeParams (juce::AudioProcessorValueTreeState& state, const EngineParams& p);

} // namespace ember
