# Offline core tools (no JUCE): quick iteration on voices and effects.
foreach(t voice_render fx_render core_smoke)
  add_executable(${t} core/${t}.cpp)
  target_link_libraries(${t} PRIVATE automata_core)
  target_include_directories(${t} PRIVATE core)
endforeach()
