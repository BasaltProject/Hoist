# Sanitizer selection. HOIST_SANITIZE is a comma-separated list: address, undefined, thread.

add_library(hoist_sanitizers INTERFACE)
add_library(hoist::sanitizers ALIAS hoist_sanitizers)

if(HOIST_SANITIZE)
    string(REPLACE "," ";" _hoist_sanitizers "${HOIST_SANITIZE}")
    if("thread" IN_LIST _hoist_sanitizers AND "address" IN_LIST _hoist_sanitizers)
        message(FATAL_ERROR "HOIST_SANITIZE: thread and address are mutually exclusive")
    endif()
    string(REPLACE ";" "," _hoist_sanitize_flag "${_hoist_sanitizers}")
    target_compile_options(hoist_sanitizers INTERFACE
        -fsanitize=${_hoist_sanitize_flag}
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )
    target_link_options(hoist_sanitizers INTERFACE -fsanitize=${_hoist_sanitize_flag})
    message(STATUS "Hoist: sanitizers enabled (${_hoist_sanitize_flag})")
endif()
