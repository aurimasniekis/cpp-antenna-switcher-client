# Each example is an executable that requires a live antenna-switcher device to
# do anything useful; running it with no arguments prints usage and exits.

set(_antenna_switcher_examples
    control
)

foreach(_ex IN LISTS _antenna_switcher_examples)
    add_executable(${_ex} ${_ex}.cpp)
    target_link_libraries(${_ex} PRIVATE antenna_switcher::client)
    antenna_switcher_set_warnings(${_ex} PRIVATE)
    antenna_switcher_enable_sanitizers(${_ex})
    antenna_switcher_enable_coverage(${_ex})
endforeach()
