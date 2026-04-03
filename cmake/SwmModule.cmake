include(CMakeParseArguments)

function(swm_add_module module_name)
    set(options)
    set(one_value_args)
    set(multi_value_args SOURCES INCLUDE_DIRS LINK_LIBS)
    cmake_parse_arguments(SWM_MOD "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT SWM_MOD_SOURCES)
        message(FATAL_ERROR "swm_add_module(${module_name}): SOURCES is required")
    endif()

    set(target "mod_${module_name}")
    add_library(${target} STATIC ${SWM_MOD_SOURCES})

    target_include_directories(${target} PUBLIC
        ${SWM_INCLUDE_DIR}
        ${SWM_MODULES_DIR}
        ${LUA_INCLUDE_DIR}
        ${SWM_MOD_INCLUDE_DIRS}
    )

    if(SWM_MOD_LINK_LIBS)
        target_link_libraries(${target} PUBLIC ${SWM_MOD_LINK_LIBS})
    endif()

    set_property(GLOBAL APPEND PROPERTY SWM_MODULE_TARGETS ${target})
endfunction()
