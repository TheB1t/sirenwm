cmake_minimum_required(VERSION 3.14)
project(libwlserver LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(XKBCOMMON      REQUIRED xkbcommon)
pkg_check_modules(XCB_COMPOSITE  REQUIRED xcb-composite)
pkg_check_modules(CAIRO          REQUIRED cairo)
pkg_check_modules(CAIROXCB       REQUIRED cairo-xcb)

list(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)

include(AddWlProtocol)

set(WLSERVER_PROTOCOL_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(XDG_SHELL_XML /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml)
set(XDG_DECO_XML  /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml)
set(XWSHELL_XML   /usr/share/wayland-protocols/staging/xwayland-shell/xwayland-shell-v1.xml)

if(NOT TARGET xcb_wrappers)
    message(FATAL_ERROR "libwlserver requires xcb_wrappers target (add libxcb first)")
endif()
if(NOT TARGET sirenwm_ipc)
    message(FATAL_ERROR "libwlserver requires sirenwm_ipc target")
endif()

add_wl_protocol(
    TARGET wlserver_proto_xdg_shell
    XML ${XDG_SHELL_XML}
    BASENAME xdg-shell
    OUT_DIR ${WLSERVER_PROTOCOL_GEN_DIR}
)

add_wl_protocol(
    TARGET wlserver_proto_xdg_decoration
    XML ${XDG_DECO_XML}
    BASENAME xdg-decoration
    OUT_DIR ${WLSERVER_PROTOCOL_GEN_DIR}
)

add_wl_protocol(
    TARGET wlserver_proto_xwayland_shell
    XML ${XWSHELL_XML}
    BASENAME xwayland-shell-v1
    OUT_DIR ${WLSERVER_PROTOCOL_GEN_DIR}
)

add_library(wlserver STATIC
    src/support/wl_display.cpp
    src/support/wl_event_loop.cpp
    src/runtime/display_server.cpp
    src/runtime/display_state.cpp
    src/runtime/overlay_manager.cpp
    src/ipc/backend_control_server.cpp
    src/protocol/output.cpp
    src/protocol/shm.cpp
    src/protocol/compositor.cpp
    src/protocol/seat.cpp
    src/protocol/xdg_shell.cpp
    src/protocol/xdg_decoration.cpp
    src/x11/output_x11.cpp
    src/x11/xwayland_shell.cpp
    src/x11/xwm.cpp
    src/x11/xwayland.cpp
)

target_include_directories(wlserver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${CAIROXCB_INCLUDE_DIRS}
)

target_link_libraries(wlserver PUBLIC
    sirenwm_ipc
    wlserver_proto_xdg_shell
    wlserver_proto_xdg_decoration
    wlserver_proto_xwayland_shell
    xcb_wrappers
    ${WAYLAND_SERVER_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    ${XCB_COMPOSITE_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${CAIROXCB_LIBRARIES}
)
