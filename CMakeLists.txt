cmake_minimum_required(VERSION 3.25)

project(antenna-switcher-client
    VERSION     0.3.0
    DESCRIPTION "Async C++17 client for the antenna-switcher device over the ESPHome native API"
    LANGUAGES   CXX
)

# Our cmake/ modules are included by absolute path rather than via
# CMAKE_MODULE_PATH: the esphome-api-client dependency ships modules with the
# same generic names (CompilerWarnings, Sanitizers, …), and putting our dir on
# the module path would shadow its own include() calls during FetchContent.
set(ANTENNA_SWITCHER_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ANTENNA_SWITCHER_BUILD_TESTS       "Build tests"                     ${PROJECT_IS_TOP_LEVEL})
option(ANTENNA_SWITCHER_BUILD_EXAMPLES    "Build examples"                  ${PROJECT_IS_TOP_LEVEL})
option(ANTENNA_SWITCHER_BUILD_DOCS        "Build Doxygen API documentation" OFF)
option(ANTENNA_SWITCHER_INSTALL           "Generate install rules"          ${PROJECT_IS_TOP_LEVEL})
# Decoupled from ANTENNA_SWITCHER_INSTALL so packaging can ship just the CLI:
# the .deb/.rpm release build turns the library install off and this on, leaving
# the CLI binary as the only staged file.
option(ANTENNA_SWITCHER_INSTALL_CLI       "Generate install rules for the antenna-switcher-cli executable" ${ANTENNA_SWITCHER_INSTALL})
option(ANTENNA_SWITCHER_ENABLE_CLANG_TIDY "Run clang-tidy during build"     OFF)
option(ANTENNA_SWITCHER_ENABLE_SANITIZERS "Build with ASan + UBSan (Debug)" OFF)
option(ANTENNA_SWITCHER_ENABLE_COVERAGE   "Build with Clang source-based coverage" OFF)
option(ANTENNA_SWITCHER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(ANTENNA_SWITCHER_BUILD_SHARED      "Build antenna-switcher-client as a shared library" OFF)
option(ANTENNA_SWITCHER_BUILD_CLI         "Build the antenna-switcher-cli command-line tool" ${PROJECT_IS_TOP_LEVEL})

include("${ANTENNA_SWITCHER_CMAKE_DIR}/CompilerWarnings.cmake")
include("${ANTENNA_SWITCHER_CMAKE_DIR}/Sanitizers.cmake")
include("${ANTENNA_SWITCHER_CMAKE_DIR}/Coverage.cmake")
include("${ANTENNA_SWITCHER_CMAKE_DIR}/ClangTidy.cmake")
include("${ANTENNA_SWITCHER_CMAKE_DIR}/Dependencies.cmake")

find_package(Threads REQUIRED)

set(ANTENNA_SWITCHER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/antenna_switcher/version.hpp.in"
    "${ANTENNA_SWITCHER_GENERATED_DIR}/include/antenna_switcher/version.hpp"
    @ONLY
)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
set(ANTENNA_SWITCHER_SOURCES
    src/client.cpp
    src/commands.cpp
)

if(ANTENNA_SWITCHER_BUILD_SHARED)
    add_library(antenna-switcher-client SHARED ${ANTENNA_SWITCHER_SOURCES})
else()
    add_library(antenna-switcher-client STATIC ${ANTENNA_SWITCHER_SOURCES})
endif()
add_library(antenna_switcher::client ALIAS antenna-switcher-client)

target_include_directories(antenna-switcher-client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${ANTENNA_SWITCHER_GENERATED_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(antenna-switcher-client PUBLIC cxx_std_17)

target_link_libraries(antenna-switcher-client
    PUBLIC
        esphome::api
        Threads::Threads
)

antenna_switcher_set_warnings(antenna-switcher-client PRIVATE)
antenna_switcher_enable_sanitizers(antenna-switcher-client)
antenna_switcher_enable_coverage(antenna-switcher-client)
antenna_switcher_enable_clang_tidy(antenna-switcher-client)

# ---------------------------------------------------------------------------
# Tests / examples / docs
# ---------------------------------------------------------------------------
if(ANTENNA_SWITCHER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ANTENNA_SWITCHER_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

if(ANTENNA_SWITCHER_BUILD_CLI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/CMakeLists.txt")
    add_subdirectory(bin)
endif()

if(ANTENNA_SWITCHER_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(ANTENNA_SWITCHER_DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )
    add_custom_target(antenna_switcher_docs
        COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating Doxygen documentation"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Install / export — only possible when the dependency was not fetched from
# source (a source-built esphome-api-client cannot be re-exported).
# ---------------------------------------------------------------------------
if(ANTENNA_SWITCHER_INSTALL AND DEFINED esphome-api-client_SOURCE_DIR)
    message(STATUS
        "antenna-switcher-client: ANTENNA_SWITCHER_INSTALL disabled — esphome-api-client was "
        "fetched from source and cannot be re-exported. Provide an installed esphome-api-client "
        "(find_package) to re-enable install rules.")
    set(ANTENNA_SWITCHER_INSTALL OFF)
endif()

if(ANTENNA_SWITCHER_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS antenna-switcher-client
        EXPORT  antenna-switcher-clientTargets
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/antenna_switcher
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )
    install(DIRECTORY "${ANTENNA_SWITCHER_GENERATED_DIR}/include/antenna_switcher"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )

    install(EXPORT antenna-switcher-clientTargets
        FILE        antenna-switcher-clientTargets.cmake
        NAMESPACE   antenna_switcher::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/antenna-switcher-client
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/antenna-switcher-clientConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/antenna-switcher-clientConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/antenna-switcher-clientConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/antenna-switcher-client
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/antenna-switcher-clientConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/antenna-switcher-clientConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/antenna-switcher-client
    )
endif()

if(PROJECT_IS_TOP_LEVEL)
    include("${ANTENNA_SWITCHER_CMAKE_DIR}/Packaging.cmake")
endif()
