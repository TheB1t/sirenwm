include(CMakeParseArguments)

function(add_wl_protocol)
    set(options)
    set(oneValueArgs TARGET XML OUT_DIR NAMESPACE WRAPPER_GENERATOR BASENAME)
    cmake_parse_arguments(WLP "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT WLP_TARGET)
        message(FATAL_ERROR "add_wl_protocol: TARGET is required")
    endif()
    if(NOT WLP_XML)
        message(FATAL_ERROR "add_wl_protocol: XML is required")
    endif()
    if(NOT EXISTS "${WLP_XML}")
        message(FATAL_ERROR "add_wl_protocol: XML not found: ${WLP_XML}")
    endif()
    if(NOT WLP_WRAPPER_GENERATOR)
        message(FATAL_ERROR "add_wl_protocol: WRAPPER_GENERATOR is required")
    endif()
    if(NOT EXISTS "${WLP_WRAPPER_GENERATOR}")
        message(FATAL_ERROR "add_wl_protocol: WRAPPER_GENERATOR not found: ${WLP_WRAPPER_GENERATOR}")
    endif()

    if(NOT WLP_OUT_DIR)
        set(WLP_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    endif()
    if(NOT WLP_NAMESPACE)
        set(WLP_NAMESPACE "wlproto")
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
    pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)

    file(MAKE_DIRECTORY "${WLP_OUT_DIR}")

    if(WLP_BASENAME)
        set(_base "${WLP_BASENAME}")
    else()
        get_filename_component(_base "${WLP_XML}" NAME_WE)
    endif()
    set(_client_h      "${WLP_OUT_DIR}/${_base}-client-protocol.h")
    set(_server_h      "${WLP_OUT_DIR}/${_base}-protocol.h")
    set(_protocol_c    "${WLP_OUT_DIR}/${_base}-protocol.c")
    set(_client_wrap_h "${WLP_OUT_DIR}/${_base}-client-wrapper.hpp")
    set(_server_wrap_h "${WLP_OUT_DIR}/${_base}-server-wrapper.hpp")
    set(_client_api_h  "${WLP_OUT_DIR}/${_base}-client-api.hpp")

    add_custom_command(
        OUTPUT  "${_client_h}"
        COMMAND ${WAYLAND_SCANNER} client-header "${WLP_XML}" "${_client_h}"
        DEPENDS "${WLP_XML}"
        COMMENT "Generating ${_base} client header"
    )

    add_custom_command(
        OUTPUT  "${_server_h}"
        COMMAND ${WAYLAND_SCANNER} server-header "${WLP_XML}" "${_server_h}"
        DEPENDS "${WLP_XML}"
        COMMENT "Generating ${_base} server header"
    )

    add_custom_command(
        OUTPUT  "${_protocol_c}"
        COMMAND ${WAYLAND_SCANNER} private-code "${WLP_XML}" "${_protocol_c}"
        DEPENDS "${WLP_XML}"
        COMMENT "Generating ${_base} protocol glue"
    )

    add_custom_command(
        OUTPUT  "${_client_wrap_h}" "${_server_wrap_h}" "${_client_api_h}"
        COMMAND ${Python3_EXECUTABLE} "${WLP_WRAPPER_GENERATOR}"
            --xml "${WLP_XML}"
            --out-client "${_client_wrap_h}"
            --out-server "${_server_wrap_h}"
            --out-client-api "${_client_api_h}"
            --namespace "${WLP_NAMESPACE}"
            --base "${_base}"
        DEPENDS "${WLP_XML}" "${WLP_WRAPPER_GENERATOR}"
        COMMENT "Generating ${_base} C++ wrappers"
    )

    add_library(${WLP_TARGET} STATIC
        "${_protocol_c}"
        "${_client_h}"
        "${_server_h}"
        "${_client_wrap_h}"
        "${_server_wrap_h}"
        "${_client_api_h}"
    )

    target_include_directories(${WLP_TARGET} PUBLIC
        "${WLP_OUT_DIR}"
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${WAYLAND_SERVER_INCLUDE_DIRS}
    )

    target_link_libraries(${WLP_TARGET} PUBLIC
        ${WAYLAND_CLIENT_LIBRARIES}
        ${WAYLAND_SERVER_LIBRARIES}
    )
endfunction()
