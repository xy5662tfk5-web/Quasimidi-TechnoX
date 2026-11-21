cmake_minimum_required(VERSION 3.22)
project(QuasimidiTechnoX VERSION 0.1.0)

# Configure with -DCMAKE_OSX_ARCHITECTURES=x86_64 for Intel Macs.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.13
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(TechnoXAssets
    SOURCES
        assets/technox_front_panel.svg
        TECHNOX.MID
        data/technox_ins.json
)

juce_add_plugin(QuasimidiTechnoX
    COMPANY_NAME "TechnoX Lab"
    BUNDLE_ID "audio.technoxlab.quasimiditechnox"
    PLUGIN_MANUFACTURER_CODE QmTx
    PLUGIN_CODE Tx01
    PRODUCT_NAME "Quasimidi TechnoX Lab"
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(QuasimidiTechnoX)

target_sources(QuasimidiTechnoX PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TechnoXPartModel.h
    Source/TechnoXSoundCatalog.h
    Source/TechnoXPerformanceLayers.h
    Source/TechnoXMidiControlRouter.h
    Source/MASSVoiceManager.h
    Source/MASSModulationMatrix.h
    Source/MASSAudioEngine.h
)

target_compile_definitions(QuasimidiTechnoX PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(QuasimidiTechnoX PRIVATE
    TechnoXAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
