cmake_minimum_required(VERSION 3.22)
project(Automata VERSION 1.0.0 LANGUAGES C CXX)

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
endif()

option(AUTOMATA_BUILD_PLUGIN "Build the JUCE plugin (VST3/Standalone/AU)" ON)
option(AUTOMATA_BUILD_TESTS  "Build unit tests and offline tools" ON)
option(AUTOMATA_COPY_PLUGIN  "Install the plugin to the user plugin folder after building" OFF)

# ---------------------------------------------------------------- core (no JUCE)
file(GLOB_RECURSE AUTOMATA_CORE_SRC CONFIGURE_DEPENDS core/*.cpp)
add_library(automata_core STATIC ${AUTOMATA_CORE_SRC})
target_include_directories(automata_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
if(MSVC)
  target_compile_options(automata_core PRIVATE /W4 /fp:fast)
  target_compile_definitions(automata_core PUBLIC _USE_MATH_DEFINES NOMINMAX)
else()
  target_compile_options(automata_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
  # No -ffast-math: NaN checks and determinism matter more than the last few percent.
  target_compile_options(automata_core PUBLIC -fno-math-errno)
endif()

# ---------------------------------------------------------------- JUCE
if(AUTOMATA_BUILD_PLUGIN)
  set(AUTOMATA_JUCE_TAG 9.0.6)
  if(DEFINED ENV{AUTOMATA_JUCE_DIR} AND EXISTS "$ENV{AUTOMATA_JUCE_DIR}/CMakeLists.txt")
    message(STATUS "Using local JUCE at $ENV{AUTOMATA_JUCE_DIR}")
    add_subdirectory("$ENV{AUTOMATA_JUCE_DIR}" juce EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    FetchContent_Declare(juce
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG ${AUTOMATA_JUCE_TAG}
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
  endif()

  set(AUTOMATA_FORMATS VST3 Standalone)
  if(APPLE)
    list(APPEND AUTOMATA_FORMATS AU)
  endif()

  juce_add_plugin(Automata
    COMPANY_NAME "Automata Audio"
    BUNDLE_ID "audio.automata.Automata"
    PLUGIN_MANUFACTURER_CODE Atma
    PLUGIN_CODE Atm8
    FORMATS ${AUTOMATA_FORMATS}
    PRODUCT_NAME "Automata"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${AUTOMATA_COPY_PLUGIN}
    VST3_CATEGORIES Instrument Drum Sampler Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

  juce_add_binary_data(AutomataAssets
    NAMESPACE AutomataAssets
    SOURCES assets/fonts/JetBrainsMono-Regular.ttf assets/fonts/JetBrainsMono-Bold.ttf)
  set_target_properties(AutomataAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

  file(GLOB_RECURSE AUTOMATA_PLUGIN_SRC CONFIGURE_DEPENDS plugin/*.cpp)
  target_sources(Automata PRIVATE ${AUTOMATA_PLUGIN_SRC})
  target_include_directories(Automata PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
  target_compile_definitions(Automata PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)
  target_link_libraries(Automata
    PRIVATE
      juce::juce_audio_utils
      juce::juce_audio_formats
      juce::juce_gui_extra
    PUBLIC
      automata_core
      AutomataAssets
      juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------- tests & tools
if(AUTOMATA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(tools)
endif()
