# The warning set the rest of Basalt uses, copied rather than shared: Hoist does not consume Wire.

add_library(hoist_warnings INTERFACE)
add_library(hoist::warnings ALIAS hoist_warnings)

target_compile_options(hoist_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
)

if(HOIST_WERROR)
    target_compile_options(hoist_warnings INTERFACE -Werror)
endif()
