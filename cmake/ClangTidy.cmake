include_guard(GLOBAL)

# antenna_switcher_enable_clang_tidy(<target>)
#
# Applies clang-tidy to <target> when ANTENNA_SWITCHER_ENABLE_CLANG_TIDY is ON.
#
# Why per-target rather than the global CMAKE_CXX_CLANG_TIDY?  Setting the
# variable globally causes every FetchContent'd dependency (esphome-api-client
# and, transitively, protobuf / abseil / libsodium / googletest) to inherit the
# clang-tidy command, and many of them fail under our `.clang-tidy` config —
# they're not our code to fix. Scoping the property to our own targets keeps the
# linter focused on antenna-switcher-client code.

if(ANTENNA_SWITCHER_ENABLE_CLANG_TIDY)
    find_program(ANTENNA_SWITCHER_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(ANTENNA_SWITCHER_CLANG_TIDY_COMMAND
        "${ANTENNA_SWITCHER_CLANG_TIDY_EXE}"
        "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-search-dirs
            OUTPUT_VARIABLE _antenna_switcher_gcc_search_dirs
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_antenna_switcher_gcc_search_dirs MATCHES "install: ([^\n]+)")
            list(APPEND ANTENNA_SWITCHER_CLANG_TIDY_COMMAND
                "--extra-arg-before=--gcc-install-dir=${CMAKE_MATCH_1}")
            message(STATUS
                "antenna-switcher-client: pinning clang-tidy to gcc install dir ${CMAKE_MATCH_1}")
        endif()
    endif()
    message(STATUS
        "antenna-switcher-client: clang-tidy enabled (${ANTENNA_SWITCHER_CLANG_TIDY_EXE})")
endif()

function(antenna_switcher_enable_clang_tidy target)
    if(NOT ANTENNA_SWITCHER_ENABLE_CLANG_TIDY)
        return()
    endif()
    set_target_properties(${target} PROPERTIES
        CXX_CLANG_TIDY "${ANTENNA_SWITCHER_CLANG_TIDY_COMMAND}"
    )
endfunction()
