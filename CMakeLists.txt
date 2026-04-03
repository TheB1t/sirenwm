cmake_minimum_required(VERSION 3.10)
project(sirenwm LANGUAGES C CXX)

find_package(Lua REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(spdlog REQUIRED)
pkg_check_modules(CAIRO      REQUIRED cairo)
pkg_check_modules(CAIROXCB   REQUIRED cairo-xcb)
pkg_check_modules(FREETYPE   REQUIRED freetype2)
pkg_check_modules(LIBPNG     REQUIRED libpng)
pkg_check_modules(XCBKEYSYMS REQUIRED xcb-keysyms)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

set(CMAKE_C_FLAGS_RELEASE   "-g0 -Ofast")
set(CMAKE_C_FLAGS_DEBUG     "-g3 -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG}")

include(cmake/SwmModule.cmake)

# ---------------------------------------------------------------------------
# Modules — auto-discovered from modules/*/module.cmake
# ---------------------------------------------------------------------------
set(SWM_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/core/include)
set(SWM_MODULES_DIR ${CMAKE_SOURCE_DIR}/modules)

add_subdirectory(modules)

get_property(SWM_MODULE_TARGETS GLOBAL PROPERTY SWM_MODULE_TARGETS)
if(NOT SWM_MODULE_TARGETS)
    message(FATAL_ERROR "No modules found. Add modules/<name>/module.cmake under modules/.")
endif()

# ---------------------------------------------------------------------------
# Core
# ---------------------------------------------------------------------------
add_library(swm_core STATIC
    core/src/config.cpp
    core/src/config_loader.cpp
    core/src/lua_host.cpp
    core/src/core/core_dispatch.cpp
    core/src/core/workspace_manager.cpp
    core/src/runtime/config_runtime.cpp
    core/src/runtime/runtime_config.cpp
    core/src/runtime/runtime.cpp
)

target_include_directories(swm_core PUBLIC
    core/include
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(swm_core PUBLIC
    spdlog::spdlog
    ${LUA_LIBRARIES}
)

# ---------------------------------------------------------------------------
# Backend
# ---------------------------------------------------------------------------
set(SWM_BACKEND "x11" CACHE STRING "Backend to build (currently only x11)")

set(SWM_BACKEND_SOURCES)
set(SWM_BACKEND_INCLUDE_DIRS)
set(SWM_BACKEND_LINK_LIBS spdlog::spdlog)

if(SWM_BACKEND STREQUAL "x11")
    include(backends/x11/backend.cmake)
else()
    message(FATAL_ERROR "Unknown SWM_BACKEND: ${SWM_BACKEND}")
endif()

add_library(swm_backend STATIC ${SWM_BACKEND_SOURCES})
target_include_directories(swm_backend PRIVATE
    core/include
    ${SWM_BACKEND_INCLUDE_DIRS}
)
target_link_libraries(swm_backend PUBLIC
    swm_core
    ${SWM_BACKEND_LINK_LIBS}
)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(${PROJECT_NAME} src/main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
    modules
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    swm_backend
    -Wl,--whole-archive
    ${SWM_MODULE_TARGETS}
    -Wl,--no-whole-archive
    dl
)
