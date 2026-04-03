# Auto-discover modules: each subdirectory containing module.cmake is a module.
# module.cmake must set SOURCES; may set LINK_LIBS and INCLUDE_DIRS.
# Any valid CMake is allowed in module.cmake (e.g. find_package).
# Module name = directory name.

file(GLOB _module_dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*")

foreach(_dir ${_module_dirs})
    if(NOT IS_DIRECTORY "${_dir}")
        continue()
    endif()
    if(NOT EXISTS "${_dir}/module.cmake")
        continue()
    endif()

    get_filename_component(_mod_name "${_dir}" NAME)

    # Reset per-module variables before include.
    set(SOURCES)
    set(LINK_LIBS)
    set(INCLUDE_DIRS)

    include("${_dir}/module.cmake")

    if(NOT SOURCES)
        message(FATAL_ERROR "module '${_mod_name}': module.cmake did not set SOURCES")
    endif()

    # Prefix sources with the module directory.
    set(_prefixed_sources)
    foreach(_src ${SOURCES})
        list(APPEND _prefixed_sources "${_dir}/${_src}")
    endforeach()

    swm_add_module(${_mod_name}
        SOURCES       ${_prefixed_sources}
        LINK_LIBS     ${LINK_LIBS}
        INCLUDE_DIRS  ${INCLUDE_DIRS}
    )
endforeach()
