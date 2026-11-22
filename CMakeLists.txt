cmake_minimum_required(VERSION 3.22)
project(QuasimidiTechnoX VERSION 0.1.0)

include(CTest)

# Configure with -DCMAKE_OSX_ARCHITECTURES=x86_64 for Intel Macs.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)

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
        technox_IC51_nm27c010_v105.bin
        technox_IC4_mx27c1000_v105.bin
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
    Source/MASSResourceROM.h
    Source/BasicMASSVoiceBackend.h
    Source/MCS51Core.h
    Source/TechnoX8032Machine.h
    Source/MCS51Timers.h
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


# MCS-51 CPU architectural foundation tests are dependency-free and do not instantiate JUCE.
if (BUILD_TESTING)
    add_executable(technox_mcs51_core_tests
        tests/test_mcs51_core.cpp
    )
    target_compile_features(technox_mcs51_core_tests PRIVATE cxx_std_17)
    target_compile_options(technox_mcs51_core_tests PRIVATE -UNDEBUG)
    add_test(NAME technox_mcs51_core_tests
        COMMAND technox_mcs51_core_tests
    )
endif()

# Intel 8032 bus and IC4 ROM-loader tests are dependency-free.
if (BUILD_TESTING)
    add_executable(technox_8032_machine_tests
        tests/test_technox_8032_machine.cpp
    )
    target_compile_features(technox_8032_machine_tests PRIVATE cxx_std_17)
    target_compile_options(technox_8032_machine_tests PRIVATE -UNDEBUG)
    add_test(NAME technox_8032_machine_tests
        COMMAND technox_8032_machine_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/technox_IC4_mx27c1000_v105.bin"
    )
endif()

# Firmware transaction decoder tests are dependency-free and do not instantiate JUCE.
if (BUILD_TESTING)
    add_executable(technox_firmware_transaction_tests
        tests/test_firmware_transaction.cpp
    )
    target_compile_features(technox_firmware_transaction_tests PRIVATE cxx_std_17)
    add_test(NAME technox_firmware_transaction_tests
        COMMAND technox_firmware_transaction_tests
    )
endif()

# Dependency-free IC51 directory, descriptor and command-stream decoder tests.
if (BUILD_TESTING)
    add_executable(technox_mass_resource_rom_tests
        tests/test_mass_resource_rom.cpp
    )
    target_compile_features(technox_mass_resource_rom_tests PRIVATE cxx_std_17)
    target_compile_options(technox_mass_resource_rom_tests PRIVATE -UNDEBUG)
    add_test(NAME technox_mass_resource_rom_tests
        COMMAND technox_mass_resource_rom_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/technox_IC51_nm27c010_v105.bin"
    )
endif()

# Dependency-free smoke test for the first audible generic voice backend.
if (BUILD_TESTING)
    add_executable(technox_basic_audio_backend_tests
        tests/test_basic_audio_backend.cpp
    )
    target_compile_features(technox_basic_audio_backend_tests PRIVATE cxx_std_17)
    add_test(NAME technox_basic_audio_backend_tests
        COMMAND technox_basic_audio_backend_tests
    )
endif()
