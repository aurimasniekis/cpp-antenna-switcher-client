include(GoogleTest)

set(_antenna_switcher_test_sources
    test_commands.cpp
)

add_executable(antenna_switcher_tests ${_antenna_switcher_test_sources})

target_link_libraries(antenna_switcher_tests
    PRIVATE
        antenna_switcher::client
        GTest::gtest_main
)

antenna_switcher_set_warnings(antenna_switcher_tests PRIVATE)
antenna_switcher_enable_sanitizers(antenna_switcher_tests)
antenna_switcher_enable_coverage(antenna_switcher_tests)
antenna_switcher_enable_clang_tidy(antenna_switcher_tests)

gtest_discover_tests(antenna_switcher_tests)
