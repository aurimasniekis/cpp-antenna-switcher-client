include_guard(GLOBAL)

# Third-party dependencies, resolved via FetchContent.
#
#   esphome-api-client -> esphome::api          (always)
#   googletest         -> GTest::gtest_main     (gated by ANTENNA_SWITCHER_BUILD_TESTS)

include(FetchContent)

# ---------------------------------------------------------------------------
# 1. esphome-api-client — the generic ESPHome native-API client this library
#    wraps. Fetched from the GitHub release tarball (v0.4.0 by default, pinned
#    with URL+URL_HASH). Pin a different release with -DANTENNA_SWITCHER_ESPHOME_API_URL=…
#    / -DANTENNA_SWITCHER_ESPHOME_API_URL_HASH=….
# ---------------------------------------------------------------------------
set(ANTENNA_SWITCHER_ESPHOME_API_URL
    "https://github.com/aurimasniekis/cpp-esphome-api/archive/refs/tags/v0.4.0.tar.gz"
    CACHE STRING "esphome-api-client release tarball URL")
set(ANTENNA_SWITCHER_ESPHOME_API_URL_HASH
    "SHA256=0c77f56a74d230d5738f90997f283d70e8b3e93e6febcf346bb8ef9dd5d6529e"
    CACHE STRING "Expected hash of the esphome-api-client release tarball")

# The wrapper only needs the generic client; keep its tests/examples/cli off.
set(ESPHOME_API_BUILD_TESTS    OFF CACHE INTERNAL "")
set(ESPHOME_API_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(ESPHOME_API_BUILD_CLI      OFF CACHE INTERNAL "")
set(ESPHOME_API_BUILD_DOCS     OFF CACHE INTERNAL "")
set(ESPHOME_API_INSTALL        OFF CACHE INTERNAL "")

message(STATUS
    "antenna-switcher-client: fetching esphome-api-client from "
    "${ANTENNA_SWITCHER_ESPHOME_API_URL}")
FetchContent_Declare(esphome-api-client
    URL      "${ANTENNA_SWITCHER_ESPHOME_API_URL}"
    URL_HASH "${ANTENNA_SWITCHER_ESPHOME_API_URL_HASH}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(esphome-api-client)

# ---------------------------------------------------------------------------
# 2. GoogleTest — tests only.
# ---------------------------------------------------------------------------
if(ANTENNA_SWITCHER_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    set(BUILD_GMOCK   OFF CACHE INTERNAL "")
    FetchContent_Declare(
        googletest
        URL      https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        FIND_PACKAGE_ARGS NAMES GTest
    )
    FetchContent_MakeAvailable(googletest)
endif()

# ---------------------------------------------------------------------------
# 3. antenna-switcher-cli dependencies — CLI tool only. These are PRIVATE to
#    the bin/ executable and never reach the library or its public surface.
#      CLI11          -> CLI11::CLI11
#      spdlog         -> spdlog::spdlog
#      nlohmann_json  -> nlohmann_json::nlohmann_json
# ---------------------------------------------------------------------------
if(ANTENNA_SWITCHER_BUILD_CLI)
    set(CLI11_BUILD_TESTS    OFF CACHE INTERNAL "")
    set(CLI11_BUILD_EXAMPLES OFF CACHE INTERNAL "")
    set(CLI11_BUILD_DOCS     OFF CACHE INTERNAL "")
    set(CLI11_INSTALL        OFF CACHE INTERNAL "")
    FetchContent_Declare(
        CLI11
        URL      https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.4.2.tar.gz
        URL_HASH SHA256=f2d893a65c3b1324c50d4e682c0cdc021dd0477ae2c048544f39eed6654b699a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        FIND_PACKAGE_ARGS NAMES CLI11
    )

    set(SPDLOG_BUILD_TESTS   OFF CACHE INTERNAL "")
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
    set(SPDLOG_INSTALL       OFF CACHE INTERNAL "")
    FetchContent_Declare(
        spdlog
        URL      https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz
        URL_HASH SHA256=1586508029a7d0670dfcb2d97575dcdc242d3868a259742b69f100801ab4e16b
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        FIND_PACKAGE_ARGS NAMES spdlog
    )

    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install    OFF CACHE INTERNAL "")
    FetchContent_Declare(
        nlohmann_json
        URL      https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
        URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        FIND_PACKAGE_ARGS NAMES nlohmann_json
    )

    FetchContent_MakeAvailable(CLI11 spdlog nlohmann_json)
endif()
