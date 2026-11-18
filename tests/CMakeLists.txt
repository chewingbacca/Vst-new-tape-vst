add_executable(verdant_render render.cpp TestSignals.h)
target_link_libraries(verdant_render PRIVATE verdant_dsp)
target_include_directories(verdant_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(verdant_tests unit_tests.cpp TestSignals.h)
target_link_libraries(verdant_tests PRIVATE verdant_dsp)
target_include_directories(verdant_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME unit_tests COMMAND verdant_tests)
add_test(NAME render_suite_quick COMMAND verdant_render --suite --quick --out ${CMAKE_BINARY_DIR}/renders_quick)
