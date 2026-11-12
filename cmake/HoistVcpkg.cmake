# Points CMake at a vcpkg checkout before project(), so the manifest in vcpkg.json is installed on configure.
# Lookup order: an explicit VCPKG_ROOT, ~/vcpkg, then a shallow clone pinned to the manifest baseline in the build tree.

if(CMAKE_TOOLCHAIN_FILE)
    return()
endif()

set(_hoist_vcpkg_root "")
foreach(_candidate IN ITEMS "$ENV{VCPKG_ROOT}" "$ENV{VCPKG_INSTALLATION_ROOT}" "$ENV{HOME}/vcpkg" "$ENV{USERPROFILE}/vcpkg")
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

set(CMAKE_TOOLCHAIN_FILE "${_hoist_vcpkg_root}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
set(VCPKG_INSTALLED_DIR "${CMAKE_SOURCE_DIR}/build/vcpkg_installed" CACHE PATH "")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND NOT VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
endif()
