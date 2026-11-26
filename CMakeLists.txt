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
    Source/MASSFirmwareResourceCommandPlanner.h
    Source/MASSFirmwarePitchParameterExecution.h
    Source/MASSOriginalVoiceProgramRuntime.h
    Source/TechnoXSoundDeviceTimeline.h
    Source/BasicMASSVoiceBackend.h
    Source/MCS51Core.h
    Source/TechnoX8032Machine.h
    Source/TechnoXPanelControlEvents.h
    Source/TechnoXPanelControlMap.h
    Source/TechnoXFirmwarePanelEvents.h
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
    # The 8032 integration test also verifies the factory Performance SysEx
    # selectors. Generate its lossless timed fixture from the checked-in MIDI
    # dump instead of silently omitting the third test argument.
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(TECHNOX_FACTORY_STREAM "${CMAKE_CURRENT_BINARY_DIR}/technox_factory_ram_bank.txsb")
    add_custom_command(
        OUTPUT "${TECHNOX_FACTORY_STREAM}"
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/parse_technox_mid.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/TECHNOX.MID"
            --serial-stream-output "${TECHNOX_FACTORY_STREAM}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/TECHNOX.MID"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/parse_technox_mid.py"
        COMMENT "Extracting timed factory Performance SysEx fixture"
        VERBATIM
    )
    add_custom_target(technox_factory_stream_fixture
        DEPENDS "${TECHNOX_FACTORY_STREAM}"
    )

    add_executable(technox_8032_machine_tests
        tests/test_technox_8032_machine.cpp
    )
    add_dependencies(technox_8032_machine_tests technox_factory_stream_fixture)
    target_compile_features(technox_8032_machine_tests PRIVATE cxx_std_17)
    target_compile_options(technox_8032_machine_tests PRIVATE -UNDEBUG)
    add_test(NAME technox_8032_machine_tests
        COMMAND technox_8032_machine_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/technox_IC4_mx27c1000_v105.bin"
            "${CMAKE_CURRENT_SOURCE_DIR}/technox_IC51_nm27c010_v105.bin"
            "${TECHNOX_FACTORY_STREAM}"
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
            "${CMAKE_CURRENT_SOURCE_DIR}/technox_IC4_mx27c1000_v105.bin"
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

# Dependency-free panel-to-audio edit mailbox tests.
if (BUILD_TESTING)
    add_executable(technox_panel_control_events_tests
        tests/test_panel_control_events.cpp
    )
    target_compile_features(technox_panel_control_events_tests PRIVATE cxx_std_17)
    target_compile_options(technox_panel_control_events_tests PRIVATE -UNDEBUG)
    add_test(NAME technox_panel_control_events_tests
        COMMAND technox_panel_control_events_tests
    )
endif()

# Dependency-free tests for manual-based mode/encoder control semantics.
if (BUILD_TESTING)
    add_executable(technox_panel_control_map_tests
        tests/test_panel_control_map.cpp
    )
    target_compile_features(technox_panel_control_map_tests PRIVATE cxx_std_17)
    target_compile_options(technox_panel_control_map_tests PRIVATE -UNDEBUG)
    add_test(NAME technox_panel_control_map_tests
        COMMAND technox_panel_control_map_tests
    )
endif()

# Dependency-free tests for IC4 firmware panel encoder event decoding.
if (BUILD_TESTING)
    add_executable(technox_firmware_panel_events_tests
        tests/test_firmware_panel_events.cpp
    )
    target_compile_features(technox_firmware_panel_events_tests PRIVATE cxx_std_17)
    target_compile_options(technox_firmware_panel_events_tests PRIVATE -UNDEBUG)
    add_test(NAME technox_firmware_panel_events_tests
        COMMAND technox_firmware_panel_events_tests
    )
endif()

# Dependency-free tests for injected external P1/P3 panel input levels.
if (BUILD_TESTING)
    add_executable(technox_panel_gpio_input_tests
        tests/test_panel_gpio_inputs.cpp
    )
    target_compile_features(technox_panel_gpio_input_tests PRIVATE cxx_std_17)
    target_compile_options(technox_panel_gpio_input_tests PRIVATE -UNDEBUG)
    add_test(NAME technox_panel_gpio_input_tests
        COMMAND technox_panel_gpio_input_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/technox_IC4_mx27c1000_v105.bin"
    )
endif()
