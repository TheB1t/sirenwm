cmake_minimum_required(VERSION 3.14)
project(libwl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
# ── common/generic thin wrappers ──

add_library(wl STATIC
    src/display.cpp
    src/event_loop.cpp
    src/shm_buffer.cpp
)

target_include_directories(wl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
)

target_link_libraries(wl PUBLIC
    ${WAYLAND_SERVER_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
)
