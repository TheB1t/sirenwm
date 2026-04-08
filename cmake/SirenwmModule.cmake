include(CMakeParseArguments)

function(sirenwm_add_module module_name)
    set(options)
    set(one_value_args)
    set(multi_value_args SOURCES INCLUDE_DIRS LINK_LIBS)
    cmake_parse_arguments(SIRENWM_MOD "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT SIRENWM_MOD_SOURCES)
        message(FATAL_ERROR "sirenwm_add_module(${module_name}): SOURCES is required")
    endif()

    set(target "mod_${module_name}")
    add_library(${target} STATIC ${SIRENWM_MOD_SOURCES})

    target_include_directories(${target} PUBLIC
        ${SIRENWM_INCLUDE_DIR}
        ${SIRENWM_MODULES_DIR}
        ${LUA_INCLUDE_DIR}
        ${SIRENWM_MOD_INCLUDE_DIRS}
    )

    if(SIRENWM_MOD_LINK_LIBS)
        target_link_libraries(${target} PUBLIC ${SIRENWM_MOD_LINK_LIBS})
    endif()

    set_property(GLOBAL APPEND PROPERTY SIRENWM_MODULE_TARGETS ${target})
endfunction()
