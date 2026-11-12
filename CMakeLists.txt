cmake_minimum_required(VERSION 3.28)

include(cmake/HoistVcpkg.cmake)

# The version the binary is named after. The build number is the CI's run number; a build made anywhere else
# has none and says so.
set(HOIST_VERSION_BASE "26.3")
if(HOIST_VERSION_OVERRIDE)
    set(HOIST_VERSION "${HOIST_VERSION_OVERRIDE}")
elseif(DEFINED ENV{GITHUB_RUN_NUMBER} AND NOT "$ENV{GITHUB_RUN_NUMBER}" STREQUAL "")
    set(HOIST_VERSION "${HOIST_VERSION_BASE}.$ENV{GITHUB_RUN_NUMBER}")
else()
    set(HOIST_VERSION "${HOIST_VERSION_BASE}.0-dev")
endif()

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

project(basalt-hoist VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    add_compile_options(-fexperimental-library)
    add_link_options(-fexperimental-library)
endif()

find_program(HOIST_CCACHE ccache)
if(HOIST_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER ${HOIST_CCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${HOIST_CCACHE})
endif()

# Hoist does not consume Wire, on purpose: it has its own build, and its dependencies come from vcpkg.
include(cmake/HoistWarnings.cmake)
include(cmake/HoistSanitizers.cmake)
include(cmake/HoistModule.cmake)
enable_testing()

find_package(MbedTLS CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)

add_subdirectory(hoist-core)
add_subdirectory(hoist-expr)
add_subdirectory(hoist-config)
add_subdirectory(hoist-net)
add_subdirectory(hoist-github)
add_subdirectory(hoist-process)
add_subdirectory(hoist-instance)
add_subdirectory(hoist-install)
add_subdirectory(hoist-runtime)
add_subdirectory(hoist-cli)
