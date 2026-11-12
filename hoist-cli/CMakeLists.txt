# The binary is named the way it is downloaded, the same as Server and Proxy, and `hoist` points at it.
if(HOIST_PLATFORM)
    set(_hoist_platform "${HOIST_PLATFORM}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_hoist_platform "macOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_hoist_platform "Windows")
else()
    set(_hoist_platform "${CMAKE_SYSTEM_NAME}")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    set(_hoist_arch "arm64")
else()
    set(_hoist_arch "x64")
endif()
set(_hoist_name "BasaltHoist-${HOIST_VERSION}-${_hoist_platform}-${_hoist_arch}")

add_executable(basalt-hoist Main.cpp)
target_link_libraries(basalt-hoist PRIVATE hoist::runtime)
target_compile_definitions(basalt-hoist PRIVATE HOIST_VERSION="${HOIST_VERSION}")
set_target_properties(basalt-hoist PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    OUTPUT_NAME "${_hoist_name}")

if(WIN32)
    add_custom_command(TARGET basalt-hoist POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:basalt-hoist>" "${CMAKE_BINARY_DIR}/bin/hoist.exe")
else()
    add_custom_command(TARGET basalt-hoist POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${_hoist_name}" "${CMAKE_BINARY_DIR}/bin/hoist")
endif()
