cmake_minimum_required(VERSION 3.14)
project(libxcb_wrappers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB       REQUIRED xcb)
pkg_check_modules(XCB_RANDR REQUIRED xcb-randr)
pkg_check_modules(X11       REQUIRED x11)
pkg_check_modules(X11_XCB   REQUIRED x11-xcb)

add_library(xcb_wrappers STATIC
    src/connection.cpp
    src/xconnection.cpp
    src/visual.cpp
)

target_include_directories(xcb_wrappers PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${XCB_INCLUDE_DIRS}
    ${XCB_RANDR_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    ${X11_XCB_INCLUDE_DIRS}
)

target_link_libraries(xcb_wrappers PUBLIC
    ${XCB_LIBRARIES}
    ${XCB_RANDR_LIBRARIES}
    ${X11_LIBRARIES}
    ${X11_XCB_LIBRARIES}
)
