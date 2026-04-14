include(CMakeParseArguments)

function(add_wl_protocol)
    set(options)
    set(oneValueArgs TARGET XML OUT_DIR BASENAME)
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

    if(NOT WLP_OUT_DIR)
        set(WLP_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    endif()

    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)

    file(MAKE_DIRECTORY "${WLP_OUT_DIR}")

    if(WLP_BASENAME)
        set(_base "${WLP_BASENAME}")
    else()
        get_filename_component(_base "${WLP_XML}" NAME_WE)
    endif()

    set(_server_h   "${WLP_OUT_DIR}/${_base}-protocol.h")
    set(_protocol_c "${WLP_OUT_DIR}/${_base}-protocol.c")

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

    add_library(${WLP_TARGET} STATIC
        "${_protocol_c}"
        "${_server_h}"
    )

    target_include_directories(${WLP_TARGET} PUBLIC
        "${WLP_OUT_DIR}"
        ${WAYLAND_SERVER_INCLUDE_DIRS}
    )

    target_link_libraries(${WLP_TARGET} PUBLIC
        ${WAYLAND_SERVER_LIBRARIES}
    )
endfunction()
