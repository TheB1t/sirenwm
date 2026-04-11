cmake_minimum_required(VERSION 3.14)
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

option(SIRENWM_DEBUG_UI "Build ImGui debug overlay (requires OpenGL/EGL)" OFF)

set(CMAKE_C_FLAGS_RELEASE   "-g0 -O3")
set(CMAKE_C_FLAGS_DEBUG     "-g3 -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG}")

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# Promote a curated set of warnings to hard errors. Keeps the build honest
# without dragging in the whole -Werror firehose (which is hostile to third-
# party headers pulled via pkg-config / FetchContent).
add_compile_options(
    -Werror=return-type
    -Werror=unused-variable
    -Werror=unused-function
    -Werror=unused-value
    -Werror=unused-but-set-variable
    -Werror=missing-field-initializers
)

include(cmake/SirenwmModule.cmake)

# ---------------------------------------------------------------------------
# Modules — auto-discovered from modules/*/module.cmake
# ---------------------------------------------------------------------------
set(SIRENWM_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/core/include)
set(SIRENWM_MODULES_DIR ${CMAKE_SOURCE_DIR}/modules)

add_subdirectory(modules)

get_property(SIRENWM_MODULE_TARGETS GLOBAL PROPERTY SIRENWM_MODULE_TARGETS)
if(NOT SIRENWM_MODULE_TARGETS)
    message(FATAL_ERROR "No modules found. Add modules/<name>/module.cmake under modules/.")
endif()

# ---------------------------------------------------------------------------
# Core
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
set(SIRENWM_LUA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/sirenwm/lua")

add_library(sirenwm_core STATIC
    core/src/config_loader.cpp
    core/src/lua_host.cpp
    core/src/module.cpp
    core/src/core/core_dispatch.cpp
    core/src/core/workspace_manager.cpp
    core/src/monitor_layout.cpp
    core/src/runtime/config_runtime.cpp
    core/src/runtime/runtime_config.cpp
    core/src/runtime/runtime.cpp
    core/src/runtime_store.cpp
)

target_include_directories(sirenwm_core PUBLIC
    core/include
    ${LUA_INCLUDE_DIR}
)

target_compile_definitions(sirenwm_core PRIVATE
    SIRENWM_LUA_DIR="${SIRENWM_LUA_DIR}"
)

target_link_libraries(sirenwm_core PUBLIC
    spdlog::spdlog
    ${LUA_LIBRARIES}
)

# ---------------------------------------------------------------------------
# Backend
# ---------------------------------------------------------------------------
set(SIRENWM_BACKEND "x11" CACHE STRING "Backend to build: x11 or wayland")

set(SIRENWM_BACKEND_SOURCES)
set(SIRENWM_BACKEND_INCLUDE_DIRS)
set(SIRENWM_BACKEND_LINK_LIBS spdlog::spdlog)
set(SIRENWM_BACKEND_COMPILE_DEFS)

if(SIRENWM_BACKEND STREQUAL "x11")
    include(backends/x11/backend.cmake)
    list(APPEND SIRENWM_BACKEND_LINK_LIBS xkbcommon)
    set(SIRENWM_BACKEND_DEFINE "SIRENWM_BACKEND_X11")
elseif(SIRENWM_BACKEND STREQUAL "wayland")
    include(backends/wayland/backend.cmake)
    set(SIRENWM_BACKEND_DEFINE "SIRENWM_BACKEND_WAYLAND")
else()
    message(FATAL_ERROR "Unknown SIRENWM_BACKEND: ${SIRENWM_BACKEND} (valid: x11, wayland)")
endif()

message(STATUS "Building with backend: ${SIRENWM_BACKEND}")

add_library(sirenwm_backend STATIC ${SIRENWM_BACKEND_SOURCES})
target_include_directories(sirenwm_backend PRIVATE
    core/include
    ${SIRENWM_BACKEND_INCLUDE_DIRS}
)
target_link_libraries(sirenwm_backend PUBLIC
    sirenwm_core
    ${SIRENWM_BACKEND_LINK_LIBS}
)
target_compile_definitions(sirenwm_backend PUBLIC
    ${SIRENWM_BACKEND_DEFINE}
    ${SIRENWM_BACKEND_COMPILE_DEFS}
)
if(SIRENWM_DEBUG_UI)
    target_compile_definitions(sirenwm_backend PUBLIC SIRENWM_DEBUG_UI)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(${PROJECT_NAME} src/main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
    modules
    ${SIRENWM_BACKEND_INCLUDE_DIRS}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ${SIRENWM_BACKEND_DEFINE}
    ${SIRENWM_BACKEND_COMPILE_DEFS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    sirenwm_backend
    -Wl,--whole-archive
    ${SIRENWM_MODULE_TARGETS}
    -Wl,--no-whole-archive
    dl
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    set(SIRENWM_TEST_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_SOURCE_DIR}/modules
        ${LUA_INCLUDE_DIR}
    )

    # --- Core tests ---
    file(GLOB SIRENWM_CORE_TEST_SOURCES tests/core/test_*.cpp)
    add_executable(sirenwm_core_tests ${SIRENWM_CORE_TEST_SOURCES})
    target_include_directories(sirenwm_core_tests PRIVATE ${SIRENWM_TEST_INCLUDE_DIRS})
    target_link_libraries(sirenwm_core_tests PRIVATE sirenwm_core GTest::gtest dl)
    add_test(NAME sirenwm_core_tests COMMAND sirenwm_core_tests)
    set_target_properties(sirenwm_core_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output
    )

    # --- Module tests ---
    file(GLOB SIRENWM_MODULE_TEST_SOURCES tests/modules/test_*.cpp)
    add_executable(sirenwm_module_tests ${SIRENWM_MODULE_TEST_SOURCES} tests/fake_backend.cpp)
    target_include_directories(sirenwm_module_tests PRIVATE ${SIRENWM_TEST_INCLUDE_DIRS})
    target_link_libraries(sirenwm_module_tests PRIVATE
        GTest::gtest
        -Wl,--whole-archive
        ${SIRENWM_MODULE_TARGETS}
        -Wl,--no-whole-archive
        sirenwm_core
        dl
    )
    add_test(NAME sirenwm_module_tests COMMAND sirenwm_module_tests)
    set_target_properties(sirenwm_module_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output
    )
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES init.lua.example init.lua.default
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sirenwm
)

install(DIRECTORY lua/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/sirenwm/lua
)

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/sirenwm.desktop.in
    ${CMAKE_BINARY_DIR}/sirenwm.desktop
    @ONLY
)
install(FILES ${CMAKE_BINARY_DIR}/sirenwm.desktop
    DESTINATION /usr/share/xsessions
)
