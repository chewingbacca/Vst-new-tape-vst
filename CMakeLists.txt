cmake_minimum_required(VERSION 3.22)

project(Verdant VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
endif()

option(VERDANT_BUILD_PLUGIN "Build the JUCE plugin (VST3 / Standalone / AU on macOS)" ON)
option(VERDANT_BUILD_TESTS  "Build the offline renderer and unit tests" ON)
option(VERDANT_COPY_PLUGIN  "Copy the built plugin to the user plugin folder" OFF)
set(VERDANT_JUCE_TAG "9.0.5" CACHE STRING "JUCE git tag to fetch")

# ------------------------------------------------------------------------------
# DSP core: plain C++20, no JUCE. Shared by the plugin, renderer and tests.
file(GLOB VERDANT_DSP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/*.h)
add_library(verdant_dsp STATIC ${VERDANT_DSP_SOURCES})
target_include_directories(verdant_dsp PUBLIC src)
target_compile_features(verdant_dsp PUBLIC cxx_std_20)
if(MSVC)
    target_compile_definitions(verdant_dsp PUBLIC _USE_MATH_DEFINES NOMINMAX)
    # no /fp:fast: it lets the compiler assume NaN never happens, and the engine's protection
    # against non-finite input must hold
    target_compile_options(verdant_dsp PRIVATE /W4)
else()
    target_compile_options(verdant_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
        -fno-math-errno -ffp-contract=fast)
endif()

# ------------------------------------------------------------------------------
if(VERDANT_BUILD_PLUGIN)
    include(FetchContent)
    # Use a local JUCE checkout with -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${VERDANT_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)

    set(VERDANT_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND VERDANT_FORMATS AU)
    endif()

    juce_add_plugin(Verdant
        COMPANY_NAME "Verdant Audio Lab"
        BUNDLE_ID "com.verdantlab.verdant"
        PLUGIN_MANUFACTURER_CODE Vrdl
        PLUGIN_CODE Vrd1
        FORMATS ${VERDANT_FORMATS}
        PRODUCT_NAME "Verdant"
        DESCRIPTION "Botanical soundscape effect"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Fx Reverb Modulation
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD ${VERDANT_COPY_PLUGIN}
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE)

    file(GLOB_RECURSE VERDANT_PLUGIN_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.h)
    target_sources(Verdant PRIVATE ${VERDANT_PLUGIN_SOURCES})

    target_compile_definitions(Verdant PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=0)

    target_link_libraries(Verdant
        PRIVATE
            verdant_dsp
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_graphics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Optional developer tool: renders the editor to PNG for visual checks.
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/ui_snapshot/CMakeLists.txt)
        add_subdirectory(tools/ui_snapshot)
    endif()
endif()

# ------------------------------------------------------------------------------
if(VERDANT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
