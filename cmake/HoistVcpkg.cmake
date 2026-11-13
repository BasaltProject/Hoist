# Points CMake at a vcpkg checkout before project(), so the manifest in vcpkg.json is installed on configure.
# Lookup order: an explicit VCPKG_ROOT, ~/vcpkg, then a shallow clone pinned to the manifest baseline in the build tree.

if(CMAKE_TOOLCHAIN_FILE)
    return()
endif()

set(_hoist_vcpkg_root "")
foreach(_candidate IN ITEMS "$ENV{VCPKG_ROOT}" "$ENV{HOME}/vcpkg" "$ENV{USERPROFILE}/vcpkg")
    if(_candidate AND EXISTS "${_candidate}/scripts/buildsystems/vcpkg.cmake")
        set(_hoist_vcpkg_root "${_candidate}")
        break()
    endif()
endforeach()

if(NOT _hoist_vcpkg_root)
    set(_hoist_vcpkg_root "${CMAKE_BINARY_DIR}/_vcpkg")
    if(NOT EXISTS "${_hoist_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
        file(READ "${CMAKE_SOURCE_DIR}/vcpkg.json" _manifest)
        string(JSON _baseline GET "${_manifest}" "builtin-baseline")
        message(STATUS "Hoist: no vcpkg found, cloning one at ${_baseline}")
        execute_process(COMMAND git init --quiet "${_hoist_vcpkg_root}" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND git -C "${_hoist_vcpkg_root}" remote add origin https://github.com/microsoft/vcpkg.git)
        execute_process(COMMAND git -C "${_hoist_vcpkg_root}" fetch --depth=1 origin "${_baseline}" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND git -C "${_hoist_vcpkg_root}" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
                        COMMAND_ERROR_IS_FATAL ANY)
    endif()
endif()

if(NOT VCPKG_TARGET_TRIPLET)
    if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(VCPKG_TARGET_TRIPLET "arm64-freebsd" CACHE STRING "")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        if("$ENV{PROCESSOR_ARCHITECTURE}" STREQUAL "ARM64" OR "$ENV{PROCESSOR_ARCHITEW6432}" STREQUAL "ARM64")
            set(VCPKG_TARGET_TRIPLET "arm64-windows-static-md" CACHE STRING "")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
        endif()
    endif()
endif()
set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_SOURCE_DIR}/vcpkg-overlays/triplets" CACHE STRING "")

# Cross-building for FreeBSD: vcpkg builds the libraries with the same compiler and sysroot as Hoist itself.
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" AND NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "FreeBSD")
    foreach(_tool CC CXX)
        if(DEFINED CMAKE_${_tool}_COMPILER AND "$ENV{${_tool}}" STREQUAL "")
            set(ENV{${_tool}} "${CMAKE_${_tool}_COMPILER}")
        endif()
    endforeach()
    set(_hoist_chainload "${CMAKE_BINARY_DIR}/hoist-freebsd-chainload.cmake")
    set(_chainload_content "set(CMAKE_SYSTEM_NAME FreeBSD)\n")
    foreach(_var CMAKE_SYSTEM_PROCESSOR CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_ASM_COMPILER
                 CMAKE_C_COMPILER_TARGET CMAKE_CXX_COMPILER_TARGET CMAKE_ASM_COMPILER_TARGET
                 CMAKE_SYSROOT CMAKE_CXX_STANDARD_LIBRARIES
                 CMAKE_FIND_ROOT_PATH_MODE_PROGRAM CMAKE_FIND_ROOT_PATH_MODE_LIBRARY
                 CMAKE_FIND_ROOT_PATH_MODE_INCLUDE CMAKE_FIND_ROOT_PATH_MODE_PACKAGE)
        if(DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
            string(APPEND _chainload_content "set(${_var} \"${${_var}}\" CACHE STRING \"\" FORCE)\n")
        endif()
    endforeach()
    string(APPEND _chainload_content "string(APPEND CMAKE_CXX_FLAGS_INIT \" -fPIC ${CMAKE_CXX_FLAGS} \")\n")
    string(APPEND _chainload_content "string(APPEND CMAKE_C_FLAGS_INIT \" -fPIC \")\n")
    string(APPEND _chainload_content "string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT \" ${CMAKE_EXE_LINKER_FLAGS} \")\n")
    string(APPEND _chainload_content "list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_CXX_STANDARD_LIBRARIES)\n")
    file(WRITE "${_hoist_chainload}" "${_chainload_content}")
    set(ENV{HOIST_VCPKG_CHAINLOAD_TOOLCHAIN} "${_hoist_chainload}")
endif()

set(CMAKE_TOOLCHAIN_FILE "${_hoist_vcpkg_root}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
set(VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/build/vcpkg_installed" CACHE PATH "")
