cmake_minimum_required(VERSION 3.22)
project(AxellPentaEQ VERSION 2.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.15
  GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(AxellPentaEQ
  COMPANY_NAME              "Axell Yobag"
  PLUGIN_MANUFACTURER_CODE  Axyb
  PLUGIN_CODE               Pnq1
  FORMATS                   VST3
  PRODUCT_NAME              "Axell Penta EQ"
  IS_SYNTH                  FALSE
  NEEDS_MIDI_INPUT          FALSE
  NEEDS_MIDI_OUTPUT         FALSE
  IS_MIDI_EFFECT            FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD   FALSE
  VST3_CATEGORIES           Fx EQ)

juce_add_binary_data(PentaBinaryData SOURCES
  Resources/fonts/Inter-Regular.otf
  Resources/fonts/Inter-SemiBold.otf
  Resources/fonts/Inter-Bold.otf)
set_target_properties(PentaBinaryData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(PENTA_PLUGIN_SOURCES
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/gui/LookAndFeel.cpp
  Source/gui/EqGraph.cpp
  Source/gui/BandPanel.cpp
  Source/gui/RackPanel.cpp
  Source/gui/PresetBrowser.cpp
  Source/gui/MatchPanel.cpp)

target_sources(AxellPentaEQ PRIVATE ${PENTA_PLUGIN_SOURCES})
target_include_directories(AxellPentaEQ PRIVATE Source)

set(PENTA_JUCE_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_compile_definitions(AxellPentaEQ PUBLIC ${PENTA_JUCE_DEFS})

target_link_libraries(AxellPentaEQ
  PRIVATE
    PentaBinaryData
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

#-------------------------------------------------------------------------------
# DSP test suite (JUCE-free: tests Source/Engine.h with plain C++17)
option(PENTA_BUILD_TESTS "Build the test suites" ON)
option(PENTA_SANITIZE "Build the test suites with AddressSanitizer + UBSan" OFF)
if(PENTA_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)
  add_executable(PentaDspTests tests/DspTests.cpp)
  target_compile_features(PentaDspTests PRIVATE cxx_std_17)
  target_compile_options(PentaDspTests PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
                         $<$<CONFIG:Release>:-O3>)
  target_compile_definitions(PentaDspTests PRIVATE
                             PENTA_TEST_BUILD_INFO="${CMAKE_BUILD_TYPE}$<$<BOOL:${PENTA_SANITIZE}>:+ASan+UBSan>")
  target_link_libraries(PentaDspTests PRIVATE Threads::Threads)
  if(PENTA_SANITIZE)
    target_compile_options(PentaDspTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
                           -fno-sanitize-recover=undefined)
    target_link_options(PentaDspTests PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME dsp_suite
           COMMAND PentaDspTests --report ${CMAKE_SOURCE_DIR}/tests/report.md
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(dsp_suite PROPERTIES TIMEOUT 3600)
endif()

#-------------------------------------------------------------------------------
# JUCE console tools: plugin tests, headless GUI snapshots, preset curve plots
function(penta_console_tool name src)
  juce_add_console_app(${name} PRODUCT_NAME "${name}")
  target_sources(${name} PRIVATE ${src} ${PENTA_PLUGIN_SOURCES})
  target_include_directories(${name} PRIVATE Source)
  target_compile_definitions(${name} PRIVATE ${PENTA_JUCE_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1
                             JucePlugin_Name="Axell Penta EQ" JucePlugin_IsSynth=0 JucePlugin_WantsMidiInput=0
                             JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
  target_link_libraries(${name} PRIVATE PentaBinaryData juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  if(PENTA_SANITIZE)
    target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
                           -fno-sanitize-recover=undefined)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

option(PENTA_BUILD_TOOLS "Build the GUI snapshot and preset curve tools" ON)
if(PENTA_BUILD_TOOLS)
  penta_console_tool(PentaGuiSnapshot tests/GuiSnapshot.cpp)
  penta_console_tool(PentaCurvePlot tests/CurvePlot.cpp)
  # independent audit host: loads the built .vst3 through JUCE's VST3 hosting code (tests/independent)
  juce_add_console_app(PentaAuditHost PRODUCT_NAME "PentaAuditHost")
  target_sources(PentaAuditHost PRIVATE tests/independent/AuditHost.cpp)
  target_compile_definitions(PentaAuditHost PRIVATE JUCE_PLUGINHOST_VST3=1 ${PENTA_JUCE_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(PentaAuditHost PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_recommended_config_flags)
endif()
if(PENTA_BUILD_TESTS)
  penta_console_tool(PentaPluginTests tests/PluginTests.cpp)
  add_test(NAME plugin_suite COMMAND PentaPluginTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
