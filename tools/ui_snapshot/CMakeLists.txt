# Developer tool: renders the Verdant editor off-screen to PNG files so the look can be
# checked without a host, and measures the paint time of the generative visual.
#
#   cmake --build <build-dir> --target verdant_ui_snapshot
#   xvfb-run -a <build-dir>/tools/ui_snapshot/verdant_ui_snapshot --out /tmp/ui --page garden
#
# Links the plugin's shared-code static library (target `Verdant`), which already contains
# the JUCE modules, so nothing is compiled twice.
add_executable(verdant_ui_snapshot main.cpp)

target_link_libraries(verdant_ui_snapshot PRIVATE Verdant verdant_dsp)

# The shared-code target links JUCE privately; re-use its include paths and definitions so
# this translation unit sees exactly the same JUCE configuration (same pattern JUCE uses
# for its own plugin wrapper targets).
target_include_directories(verdant_ui_snapshot PRIVATE
    $<TARGET_PROPERTY:Verdant,INCLUDE_DIRECTORIES>
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(verdant_ui_snapshot PRIVATE
    $<TARGET_PROPERTY:Verdant,COMPILE_DEFINITIONS>)
