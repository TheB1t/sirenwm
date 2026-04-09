find_package(PkgConfig REQUIRED)

pkg_check_modules(WLROOTS   REQUIRED wlroots)
pkg_check_modules(WAYLAND   REQUIRED wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
pkg_check_modules(CAIRO     REQUIRED cairo)
pkg_check_modules(DRM       REQUIRED libdrm)
pkg_check_modules(PIXMAN    REQUIRED pixman-1)

# ---------------------------------------------------------------------------
# Generate protocol headers via wayland-scanner
# ---------------------------------------------------------------------------
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_get_variable(WLROOTS_PROTOCOLS_DIR wlroots pkgdatadir)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/wl_gen")

# xdg-shell (stable, from wayland-protocols)
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
set(XDG_SHELL_HDR "${CMAKE_BINARY_DIR}/wl_gen/xdg-shell-protocol.h")
execute_process(
    COMMAND ${WAYLAND_SCANNER} server-header ${XDG_SHELL_XML} ${XDG_SHELL_HDR}
    RESULT_VARIABLE _wl_result
)
if(NOT _wl_result EQUAL 0)
    message(FATAL_ERROR "wayland-scanner failed to generate xdg-shell-protocol.h")
endif()

# wlr-layer-shell-unstable-v1 (from wlroots protocols dir)
if(WLROOTS_PROTOCOLS_DIR)
    set(LAYER_SHELL_XML "${WLROOTS_PROTOCOLS_DIR}/wlr-layer-shell-unstable-v1.xml")
else()
    # Fallback: look in common locations
    foreach(_dir
        /usr/share/wlroots/protocols
        /usr/local/share/wlroots/protocols
        /usr/share/wlr-protocols
    )
        if(EXISTS "${_dir}/wlr-layer-shell-unstable-v1.xml")
            set(LAYER_SHELL_XML "${_dir}/wlr-layer-shell-unstable-v1.xml")
            break()
        endif()
    endforeach()
endif()

if(LAYER_SHELL_XML AND EXISTS "${LAYER_SHELL_XML}")
    set(LAYER_SHELL_HDR "${CMAKE_BINARY_DIR}/wl_gen/wlr-layer-shell-unstable-v1-protocol.h")
    execute_process(
        COMMAND ${WAYLAND_SCANNER} server-header ${LAYER_SHELL_XML} ${LAYER_SHELL_HDR}
        RESULT_VARIABLE _wl_result
    )
    if(NOT _wl_result EQUAL 0)
        message(FATAL_ERROR "wayland-scanner failed to generate wlr-layer-shell-unstable-v1-protocol.h")
    endif()
    message(STATUS "Generated wlr-layer-shell-unstable-v1-protocol.h from ${LAYER_SHELL_XML}")
else()
    message(WARNING "wlr-layer-shell-unstable-v1.xml not found — layer-shell support disabled")
    list(APPEND SIRENWM_BACKEND_COMPILE_DEFS SIRENWM_NO_LAYER_SHELL)
endif()

list(APPEND SIRENWM_BACKEND_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/loop.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/xdg_shell.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/monitor_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/render_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/input_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/keyboard_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/wl_cpu_buffer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/wl_surface.cpp
)

list(APPEND SIRENWM_BACKEND_INCLUDE_DIRS
    ${CMAKE_BINARY_DIR}/wl_gen        # generated protocol headers
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${WLROOTS_INCLUDE_DIRS}
    ${WAYLAND_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${DRM_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
)

list(APPEND SIRENWM_BACKEND_LINK_LIBS
    ${WLROOTS_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${DRM_LIBRARIES}
    ${PIXMAN_LIBRARIES}
    EGL
    gbm
)

# wlroots gates all unstable APIs behind this macro.
list(APPEND SIRENWM_BACKEND_COMPILE_DEFS WLR_USE_UNSTABLE)
