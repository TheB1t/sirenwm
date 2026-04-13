cmake_minimum_required(VERSION 3.14)
project(libwlproto LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET wl)
    message(FATAL_ERROR "libwlproto requires wl target (add libwl first)")
endif()

list(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)

include(AddWlProtocol)

set(WLPROTO_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_wl_wrappers.py)
set(WLPROTO_GEN_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(XDG_SHELL_XML /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml)
set(XDG_DECO_XML  /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml)
set(XWSHELL_XML   /usr/share/wayland-protocols/staging/xwayland-shell/xwayland-shell-v1.xml)

add_wl_protocol(
    TARGET wlproto_xdg_shell
    XML ${XDG_SHELL_XML}
    BASENAME xdg-shell
    OUT_DIR ${WLPROTO_GEN_DIR}
    NAMESPACE wlproto
    WRAPPER_GENERATOR ${WLPROTO_GENERATOR}
)

add_wl_protocol(
    TARGET wlproto_xdg_decoration
    XML ${XDG_DECO_XML}
    BASENAME xdg-decoration
    OUT_DIR ${WLPROTO_GEN_DIR}
    NAMESPACE wlproto
    WRAPPER_GENERATOR ${WLPROTO_GENERATOR}
)

add_wl_protocol(
    TARGET wlproto_xwayland_shell
    XML ${XWSHELL_XML}
    BASENAME xwayland-shell-v1
    OUT_DIR ${WLPROTO_GEN_DIR}
    NAMESPACE wlproto
    WRAPPER_GENERATOR ${WLPROTO_GENERATOR}
)

add_wl_protocol(
    TARGET wlproto_sirenwm_admin
    XML ${CMAKE_CURRENT_SOURCE_DIR}/protocols/sirenwm-server-v1.xml
    BASENAME sirenwm-server-v1
    OUT_DIR ${WLPROTO_GEN_DIR}
    NAMESPACE wlproto
    WRAPPER_GENERATOR ${WLPROTO_GENERATOR}
)

add_library(wlproto INTERFACE)
target_link_libraries(wlproto INTERFACE
    wl
    wlproto_xdg_shell
    wlproto_xdg_decoration
    wlproto_xwayland_shell
    wlproto_sirenwm_admin
)
