# hoist_module(<name> SOURCES <files...> [DEPENDS <hoist modules...>] [LIBS <libraries...>])
# creates the static library hoist-<name> (alias hoist::<name>) with its headers under <name>/include.
function(hoist_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS;LIBS" ${ARGN})
    set(target hoist-${name})
    add_library(${target} STATIC ${ARG_SOURCES})
    add_library(hoist::${name} ALIAS ${target})
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(${target} PUBLIC hoist::warnings hoist::sanitizers)
    foreach(dep IN LISTS ARG_DEPENDS)
        target_link_libraries(${target} PUBLIC hoist::${dep})
    endforeach()
    if(ARG_LIBS)
        target_link_libraries(${target} PUBLIC ${ARG_LIBS})
    endif()
endfunction()

# hoist_test(<name> SOURCE <file> LIBS <libraries...>) builds and registers a doctest executable.
function(hoist_test name)
    cmake_parse_arguments(ARG "" "SOURCE" "LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCE})
    target_link_libraries(${name} PRIVATE ${ARG_LIBS} doctest::doctest)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# hoist_embed_text(<name> NAMESPACE <ns> HEADER <path> SUFFIX <suffix> FILES <files...>) turns text files into
# string constants (`backup.toml` becomes `kBackup<suffix>`) so a release stays one self-contained executable.
function(hoist_embed_text name)
    cmake_parse_arguments(ARG "" "NAMESPACE;HEADER;SUFFIX" "FILES" ${ARGN})
    set(generated ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(header ${generated}/include/${ARG_HEADER})
    set(source ${generated}/${name}.cpp)
    add_custom_command(
        OUTPUT ${header} ${source}
        COMMAND ${CMAKE_COMMAND} -DOUT_HEADER=${header} -DOUT_SOURCE=${source} -DHEADER_INCLUDE=${ARG_HEADER}
                -DTEXT_NAMESPACE=${ARG_NAMESPACE} -DNAME_SUFFIX=${ARG_SUFFIX} "-DTEXT_FILES=${ARG_FILES}"
                -P ${CMAKE_SOURCE_DIR}/cmake/HoistEmbedText.cmake
        DEPENDS ${ARG_FILES} ${CMAKE_SOURCE_DIR}/cmake/HoistEmbedText.cmake
        COMMENT "Embedding ${name}"
        VERBATIM)
    add_library(${name} STATIC ${source} ${header})
    target_include_directories(${name} PUBLIC ${generated}/include)
endfunction()
