cmake_minimum_required(VERSION 3.22)

project(Swoon VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SWOON_BUILD_PLUGIN "Build the JUCE plugin (VST3 / Standalone / AU on macOS)" ON)
option(SWOON_BUILD_TESTS  "Build the offline render harness and unit tests" ON)
option(SWOON_COPY_PLUGIN  "Copy the built plugin into the user's plugin folder" OFF)
set(SWOON_JUCE_TAG "9.0.6" CACHE STRING "JUCE git tag to fetch")

# ---------------------------------------------------------------------------
# DSP core: plain C++20, no JUCE. Everything that makes sound lives here so it
# can be rendered and unit-tested without a plugin host.
# ---------------------------------------------------------------------------
add_library(swoon_dsp STATIC
    Source/dsp/Params.cpp
    Source/dsp/Presets.cpp
    Source/dsp/Engine.cpp)
target_include_directories(swoon_dsp PUBLIC Source)
if(MSVC)
    target_compile_options(swoon_dsp PRIVATE /W4 /fp:fast)
else()
    target_compile_options(swoon_dsp PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
        -fno-math-errno -fno-trapping-math)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(SWOON_BUILD_PLUGIN)
    include(FetchContent)
    # Use -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to build against a local checkout.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${SWOON_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    set(SWOON_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND SWOON_FORMATS AU)
    endif()

    juce_add_plugin(Swoon
        COMPANY_NAME "Half Light Audio"
        BUNDLE_ID "audio.halflight.swoon"
        PLUGIN_MANUFACTURER_CODE Hlfl
        PLUGIN_CODE Swn1
        FORMATS ${SWOON_FORMATS}
        PRODUCT_NAME "Swoon"
        DESCRIPTION "Shoegaze wall-of-sound effect"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Fx Distortion Reverb Modulation
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD ${SWOON_COPY_PLUGIN}
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE)

    file(GLOB SWOON_UI_SOURCES CONFIGURE_DEPENDS Source/ui/*.cpp Source/ui/*.h)
    target_sources(Swoon PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PresetManager.cpp
        Source/PresetManager.h
        ${SWOON_UI_SOURCES})

    file(GLOB SWOON_ASSETS CONFIGURE_DEPENDS Assets/*)
    if(SWOON_ASSETS)
        juce_add_binary_data(SwoonAssets SOURCES ${SWOON_ASSETS})
        target_link_libraries(Swoon PRIVATE SwoonAssets)
        target_compile_definitions(Swoon PUBLIC SWOON_HAS_ASSETS=1)
    endif()

    target_compile_definitions(Swoon PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(Swoon
        PUBLIC
            swoon_dsp
            juce::juce_audio_utils
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Headless editor renderer used for visual review of the UI (not shipped).
    if(SWOON_BUILD_TESTS)
        # A plain executable on top of the plugin's shared code (which already carries the JUCE modules).
        add_executable(swoon_ui_snapshot Tests/ui_snapshot.cpp)
        target_link_libraries(swoon_ui_snapshot PRIVATE Swoon)
    endif()
endif()

# ---------------------------------------------------------------------------
# Offline harness + unit tests (no JUCE)
# ---------------------------------------------------------------------------
if(SWOON_BUILD_TESTS)
    enable_testing()
    add_executable(swoon_render Tests/render_main.cpp)
    target_link_libraries(swoon_render PRIVATE swoon_dsp)

    add_executable(swoon_tests Tests/unit_tests.cpp)
    target_link_libraries(swoon_tests PRIVATE swoon_dsp)
    add_test(NAME unit COMMAND swoon_tests)
endif()
