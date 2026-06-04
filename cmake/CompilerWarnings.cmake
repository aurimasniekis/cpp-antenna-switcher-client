include_guard(GLOBAL)

# antenna_switcher_set_warnings(<target> [PUBLIC|PRIVATE|INTERFACE])
#
# Applies a strict warning set per compiler. Adds -Werror / /WX when
# ANTENNA_SWITCHER_WARNINGS_AS_ERRORS is ON.
function(antenna_switcher_set_warnings target)
    set(visibility "PRIVATE")
    if(ARGC GREATER 1)
        set(visibility "${ARGV1}")
    endif()

    set(_clang_gcc_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )

    set(_msvc_warnings
        /W4
        /permissive-
        /w14242 /w14254 /w14263 /w14265 /w14287
        /we4289 /w14296 /w14311 /w14545 /w14546
        /w14547 /w14549 /w14555 /w14619 /w14640
        /w14826 /w14905 /w14906 /w14928
    )

    if(ANTENNA_SWITCHER_WARNINGS_AS_ERRORS)
        list(APPEND _clang_gcc_warnings -Werror)
        list(APPEND _msvc_warnings     /WX)
    endif()

    if(MSVC)
        target_compile_options(${target} ${visibility} ${_msvc_warnings})
    else()
        target_compile_options(${target} ${visibility} ${_clang_gcc_warnings})
    endif()
endfunction()
