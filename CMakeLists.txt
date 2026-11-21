cmake_minimum_required(VERSION 3.22)
project(Splinter VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

option(SPLINTER_BUILD_TESTS "Build unit tests and the offline render harness" ON)
option(SPLINTER_LTO "Link-time optimisation (slow links, slightly faster DSP)" OFF)
option(SPLINTER_COPY_AFTER_BUILD "Copy the plugin into the user's plugin folder after building" OFF)
set(SPLINTER_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout (otherwise JUCE is fetched)")
set(SPLINTER_JUCE_TAG "9.0.6" CACHE STRING "JUCE git tag to fetch")

# ---------------------------------------------------------------- JUCE
if(SPLINTER_JUCE_PATH AND EXISTS "${SPLINTER_JUCE_PATH}/CMakeLists.txt")
    message(STATUS "Splinter: using JUCE at ${SPLINTER_JUCE_PATH}")
    add_subdirectory("${SPLINTER_JUCE_PATH}" juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    message(STATUS "Splinter: fetching JUCE ${SPLINTER_JUCE_TAG}")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${SPLINTER_JUCE_TAG}
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SPLINTER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SPLINTER_FORMATS AU)
endif()

juce_add_plugin(Splinter
    COMPANY_NAME "Splinter Audio"
    BUNDLE_ID "audio.splinter.Splinter"
    PLUGIN_MANUFACTURER_CODE Spln
    PLUGIN_CODE Spl1
    FORMATS ${SPLINTER_FORMATS}
    PRODUCT_NAME "Splinter"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    COPY_PLUGIN_AFTER_BUILD ${SPLINTER_COPY_AFTER_BUILD}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

file(GLOB_RECURSE SPLINTER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")
target_sources(Splinter PRIVATE ${SPLINTER_SOURCES})
target_include_directories(Splinter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(Splinter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(Splinter
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags)
if(SPLINTER_LTO)
    target_link_libraries(Splinter PUBLIC juce::juce_recommended_lto_flags)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Splinter PRIVATE -Wall -Wextra -Wno-sign-compare)
endif()

# ---------------------------------------------------------------- tests
if(SPLINTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
