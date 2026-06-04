include_guard(GLOBAL)

set(ANTENNA_SWITCHER_SANITIZER_FLAGS
    -fsanitize=address
    -fsanitize=undefined
    -fno-omit-frame-pointer
    -fno-sanitize-recover=all
)

# Apply sanitizer flags at directory scope so they propagate to every target
# declared after this module is included — including the FetchContent-built
# esphome-api-client dependency. Mixed instrumentation otherwise causes libc++
# container annotations to get out of sync between our code and uninstrumented
# deps, producing spurious container-overflow reports.
if(ANTENNA_SWITCHER_ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(${ANTENNA_SWITCHER_SANITIZER_FLAGS})
    add_link_options   (${ANTENNA_SWITCHER_SANITIZER_FLAGS})
elseif(ANTENNA_SWITCHER_ENABLE_SANITIZERS AND MSVC)
    message(STATUS "antenna-switcher-client: sanitizers requested but skipped on MSVC")
endif()

# antenna_switcher_enable_sanitizers(<target>)
#
# Adds AddressSanitizer + UndefinedBehaviorSanitizer flags to <target> when
# ANTENNA_SWITCHER_ENABLE_SANITIZERS is ON and the toolchain is GCC or Clang. The
# directory-scope add_compile_options above already covers all targets in this
# project; this function is kept as a no-op safety net for targets declared in
# unusual scopes.
function(antenna_switcher_enable_sanitizers target)
    if(NOT ANTENNA_SWITCHER_ENABLE_SANITIZERS)
        return()
    endif()
    if(MSVC)
        return()
    endif()

    target_compile_options(${target} PRIVATE ${ANTENNA_SWITCHER_SANITIZER_FLAGS})
    target_link_options   (${target} PRIVATE ${ANTENNA_SWITCHER_SANITIZER_FLAGS})
endfunction()
