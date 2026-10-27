cmake_minimum_required(VERSION 3.22)
project(UNDERTOW VERSION 1.0.0)

# Visual Studio: put the C++ runtime inside the plugin (no Visual C++ Redistributable needed)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to JUCE 8 (git clone --branch 8.0.4 https://github.com/juce-framework/JUCE.git)
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_DIR} juce)

option(UNDERTOW_TESTS "Build the offline test runner" OFF)

set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(Undertow
    COMPANY_NAME "Purra Audio"
    COMPANY_WEBSITE ""
    BUNDLE_ID com.purraaudio.undertow
    PLUGIN_MANUFACTURER_CODE Prra
    PLUGIN_CODE Undt
    PRODUCT_NAME "Undertow"
    DESCRIPTION "Kick-restarted sidechain curve, sub phase alignment and dynamic EQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS ${FORMATS})

juce_generate_juce_header(Undertow)

add_library(pffft STATIC third_party/pffft/pffft.c third_party/pffft/pffft_common.c)
target_include_directories(pffft PUBLIC third_party/pffft)
target_compile_definitions(pffft PUBLIC PFFFT_STATIC_DEFINE)
set_target_properties(pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(pffft PRIVATE -O3)
endif()

target_sources(Undertow PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Undertow PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_REPORT_APP_USAGE=0)

if(UNIX AND NOT APPLE)
  # headless-friendly Linux build (used for the offline tests)
  target_compile_definitions(Undertow PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
endif()

target_link_libraries(Undertow
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        pffft
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MINGW)
  # llvm-mingw does not honour #pragma comment(lib); list the system libraries explicitly
  set(WINLIBS advapi32 comctl32 d2d1 d3d11 dbghelp dcomp dwmapi dwrite dxgi dxguid imm32 kernel32
      shell32 shlwapi user32 version winmm wininet ws2_32 ole32 oleaut32 uuid gdi32 comdlg32 rpcrt4
      setupapi ksguid ksuser sapi)
  target_link_libraries(Undertow PUBLIC ${WINLIBS})
endif()

if(UNDERTOW_TESTS)
  juce_add_console_app(UndertowTests PRODUCT_NAME "UndertowTests")
  juce_generate_juce_header(UndertowTests)
  target_sources(UndertowTests PRIVATE tests/Tests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_include_directories(UndertowTests PRIVATE Source)
  target_compile_definitions(UndertowTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0
      JucePlugin_Name="Undertow" JUCE_USE_WINDOWS_MEDIA_FORMAT=0)
  target_link_libraries(UndertowTests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags pffft)
endif()
