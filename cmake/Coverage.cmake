include_guard(GLOBAL)

set(ANTENNA_SWITCHER_COVERAGE_FLAGS -fprofile-instr-generate -fcoverage-mapping)

# Apply coverage flags at directory scope so executables that link our
# instrumented static libs also get -fprofile-instr-generate on their own link
# command — without it, clang's driver won't pull in the LLVM profile runtime
# and the link fails with undefined __llvm_profile_runtime_user.
if(ANTENNA_SWITCHER_ENABLE_COVERAGE AND NOT MSVC)
    add_compile_options(${ANTENNA_SWITCHER_COVERAGE_FLAGS} -O0 -g)
    add_link_options   (${ANTENNA_SWITCHER_COVERAGE_FLAGS})
elseif(ANTENNA_SWITCHER_ENABLE_COVERAGE AND MSVC)
    message(STATUS "antenna-switcher-client: coverage requested but skipped on MSVC")
endif()

# antenna_switcher_enable_coverage(<target>)
#
# Adds Clang source-based coverage flags to <target> when
# ANTENNA_SWITCHER_ENABLE_COVERAGE is ON. The directory-scope add_compile_options
# above already covers all targets in this project; this function is kept as a
# safety net.
function(antenna_switcher_enable_coverage target)
    if(NOT ANTENNA_SWITCHER_ENABLE_COVERAGE)
        return()
    endif()
    if(MSVC)
        return()
    endif()

    target_compile_options(${target} PRIVATE ${ANTENNA_SWITCHER_COVERAGE_FLAGS} -O0 -g)
    target_link_options   (${target} PRIVATE ${ANTENNA_SWITCHER_COVERAGE_FLAGS})
endfunction()
