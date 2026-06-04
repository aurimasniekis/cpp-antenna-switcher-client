# antenna-switcher-cli — the command-line tool. Built only when
# ANTENNA_SWITCHER_BUILD_CLI is ON. Its dependencies (CLI11 / spdlog /
# nlohmann_json) are PRIVATE here and never reach the antenna_switcher::client
# library target or its public surface.

set(ANTENNA_SWITCHER_CLI_SOURCES
    main.cpp
    cli/options.cpp
    cli/output.cpp
    cli/config.cpp
    cli/signal.cpp
    cli/commands.cpp
    cli/interactive.cpp
)

add_executable(antenna-switcher-cli ${ANTENNA_SWITCHER_CLI_SOURCES})

target_include_directories(antenna-switcher-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(antenna-switcher-cli
    PRIVATE
        antenna_switcher::client
        CLI11::CLI11
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

antenna_switcher_set_warnings(antenna-switcher-cli PRIVATE)
# nlohmann/json triggers a false-positive -Wnull-dereference inside its own
# headers under some GCC versions at -O2; keep -Werror everywhere else but don't
# let that third-party-header warning fail the CLI build.
target_compile_options(antenna-switcher-cli PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wno-error=null-dereference>)
# MSVC flags the standard std::getenv (used to read ANTENNA_SWITCHER_CLI_* env
# vars) as C4996 "deprecated/unsafe"; under /WX that fails the build. The call is
# correct and portable, so silence the CRT secure-function deprecation here.
target_compile_definitions(antenna-switcher-cli PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
antenna_switcher_enable_sanitizers(antenna-switcher-cli)
antenna_switcher_enable_coverage(antenna-switcher-cli)
antenna_switcher_enable_clang_tidy(antenna-switcher-cli)

if(ANTENNA_SWITCHER_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS antenna-switcher-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
