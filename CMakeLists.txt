cmake_minimum_required(VERSION 3.22)
project(Halcyon VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# Use a local JUCE checkout if given (-DJUCE_DIR=/path/to/JUCE), otherwise download it.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(HALCYON_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND HALCYON_FORMATS AU)
endif()

juce_add_plugin(Halcyon
    COMPANY_NAME             "AcidLab"
    PRODUCT_NAME             "Halcyon"
    BUNDLE_ID                "com.acidlab.halcyon"
    PLUGIN_MANUFACTURER_CODE Acdl
    PLUGIN_CODE              Hlcy
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES          Instrument Synth
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD  FALSE
    FORMATS                  ${HALCYON_FORMATS})

target_sources(Halcyon PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(Halcyon PRIVATE Source)

target_compile_definitions(Halcyon PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Halcyon
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional headless test: renders audio through the full plugin and snapshots the UI
option(HALCYON_BUILD_TESTS "Build the plugin smoke test" OFF)
if(HALCYON_BUILD_TESTS)
    add_executable(HalcyonSmokeTest tests/smoke_test.cpp)
    target_include_directories(HalcyonSmokeTest PRIVATE Source $<TARGET_PROPERTY:Halcyon,INCLUDE_DIRECTORIES>)
    target_compile_definitions(HalcyonSmokeTest PRIVATE $<TARGET_PROPERTY:Halcyon,COMPILE_DEFINITIONS>)
    target_link_libraries(HalcyonSmokeTest PRIVATE Halcyon juce::juce_recommended_config_flags)
endif()

# Preset lab: renders presets with the DOA1 engine without JUCE. Used by tools/tune_presets.py
# and by the DOA1 MCP sound matcher (tools/doa1_mcp).
add_executable(preset_lab tools/preset_lab.cpp)
target_compile_features(preset_lab PRIVATE cxx_std_17)
