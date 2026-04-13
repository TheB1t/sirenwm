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

if(NOT TARGET xcb_wrappers)
    message(FATAL_ERROR "libwlserver requires xcb_wrappers target (add libxcb first)")
endif()
if(NOT TARGET wlproto OR NOT TARGET wlproto_xdg_shell
   OR NOT TARGET wlproto_xdg_decoration
   OR NOT TARGET wlproto_xwayland_shell
   OR NOT TARGET wlproto_sirenwm_admin)
    message(FATAL_ERROR "libwlserver requires wlproto and protocol targets (add libwlproto first)")
endif()

add_library(wlserver STATIC
    src/display_server.cpp
    src/overlay_manager.cpp
    src/admin.cpp
    src/output_x11.cpp
    src/output.cpp
    src/shm.cpp
    src/compositor.cpp
    src/seat.cpp
    src/xdg_shell.cpp
    src/xdg_decoration.cpp
    src/xwayland_shell.cpp
    src/xwm.cpp
    src/xwayland.cpp
)

target_include_directories(wlserver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libwl/include
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${CAIROXCB_INCLUDE_DIRS}
)

target_link_libraries(wlserver PUBLIC
    wlproto
    wlproto_xdg_shell
    wlproto_xdg_decoration
    wlproto_xwayland_shell
    wlproto_sirenwm_admin
    xcb_wrappers
    ${WAYLAND_SERVER_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    ${XCB_COMPOSITE_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${CAIROXCB_LIBRARIES}
)
