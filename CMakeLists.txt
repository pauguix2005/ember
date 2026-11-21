cmake_minimum_required(VERSION 3.22)

project(EMBER VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

# Universal binary on macOS (Intel + Apple Silicon) unless the caller overrides it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

# JUCE: use a local checkout in ./JUCE if present, otherwise fetch the pinned tag.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

option(EMBER_BUILD_TESTS "Build the offline DSP test runner" ON)
option(EMBER_TESTS_ONLY "Only build the DSP test runner (no plugin)" OFF)

set(EMBER_DSP_SOURCES
    Source/dsp/EmberEngine.cpp
    Source/Parameters.cpp
    Source/Presets.cpp)

if(NOT EMBER_TESTS_ONLY)
set(EMBER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND EMBER_FORMATS AU)
endif()
# AAX is planned for v1.1 and needs the Avid SDK + PACE signing:
# juce_set_aax_sdk_path(<path>) and add AAX to EMBER_FORMATS.

juce_add_plugin(Ember
    COMPANY_NAME "Ember Audio"
    COMPANY_CODE Embr
    PLUGIN_MANUFACTURER_CODE Embr
    PLUGIN_CODE Embr
    BUNDLE_ID com.emberaudio.ember
    PRODUCT_NAME "EMBER"
    DESCRIPTION "Warmth for house basslines"
    VERSION ${PROJECT_VERSION}
    FORMATS ${EMBER_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(EmberBinaryData SOURCES
    resources/fonts/SpaceGrotesk.ttf
    resources/fonts/JetBrainsMono.ttf)

target_sources(Ember PRIVATE
    ${EMBER_DSP_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/EmberLookAndFeel.cpp
    Source/ui/Knob.cpp
    Source/ui/Analyzer.cpp
    Source/ui/WarmthKnob.cpp
    Source/ui/ModulePanels.cpp)

target_include_directories(Ember PRIVATE Source)

target_compile_definitions(Ember PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Ember
    PRIVATE
        EmberBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

if(EMBER_BUILD_TESTS)
    juce_add_console_app(EmberTests PRODUCT_NAME "EmberTests")
    target_sources(EmberTests PRIVATE tests/EmberTests.cpp ${EMBER_DSP_SOURCES})
    target_include_directories(EmberTests PRIVATE Source)
    target_compile_definitions(EmberTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(EmberTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

option(EMBER_BUILD_TOOLS "Build the UI snapshot tool (development only)" OFF)
if(EMBER_BUILD_TOOLS AND NOT EMBER_TESTS_ONLY)
    juce_add_console_app(EmberSnapshot PRODUCT_NAME "EmberSnapshot")
    target_sources(EmberSnapshot PRIVATE
        tools/Snapshot.cpp
        ${EMBER_DSP_SOURCES}
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ui/EmberLookAndFeel.cpp
        Source/ui/Knob.cpp
        Source/ui/Analyzer.cpp
        Source/ui/WarmthKnob.cpp
        Source/ui/ModulePanels.cpp)
    target_include_directories(EmberSnapshot PRIVATE Source)
    target_compile_definitions(EmberSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_VersionString="${PROJECT_VERSION}" JucePlugin_Name="EMBER" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(EmberSnapshot PRIVATE EmberBinaryData juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(EMBER_BUILD_TOOLS)
    juce_add_console_app(EmberHostTest PRODUCT_NAME "EmberHostTest")
    target_sources(EmberHostTest PRIVATE tools/HostTest.cpp)
    target_compile_definitions(EmberHostTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(EmberHostTest PRIVATE juce::juce_audio_processors juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)
endif()
