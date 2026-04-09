find_package(PkgConfig REQUIRED)

# wlroots pkg-config module name varies by distro/version:
#   Arch/Ubuntu: "wlroots"
#   Debian trixie: "wlroots-0.18"
# wlroots pkg-config module name varies by distro/version:
#   Arch wlroots0.17: "wlroots-0.17"
#   Arch wlroots0.18: "wlroots-0.18"
#   Debian trixie:    "wlroots-0.18"
#   Ubuntu 24.04:     "wlroots"
foreach(_wlr_name wlroots wlroots-0.18 wlroots-0.17)
    pkg_check_modules(WLROOTS ${_wlr_name})
    if(WLROOTS_FOUND)
        break()
    endif()
endforeach()
if(NOT WLROOTS_FOUND)
    message(FATAL_ERROR "wlroots not found (tried: wlroots, wlroots-0.18, wlroots-0.17)")
endif()
message(STATUS "Found wlroots: ${WLROOTS_VERSION}")
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
set(LAYER_SHELL_XML "")
# Try pkgdatadir from wlroots pkg first
if(WLROOTS_PROTOCOLS_DIR)
    if(EXISTS "${WLROOTS_PROTOCOLS_DIR}/wlr-layer-shell-unstable-v1.xml")
        set(LAYER_SHELL_XML "${WLROOTS_PROTOCOLS_DIR}/wlr-layer-shell-unstable-v1.xml")
    endif()
endif()
# Fallback: common filesystem locations
if(NOT LAYER_SHELL_XML)
    foreach(_dir
        /usr/share/wlroots/protocol
        /usr/share/wlroots/protocols
        /usr/local/share/wlroots/protocol
        /usr/share/wlr-protocols
        /usr/local/share/wlr-protocols
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

# ---------------------------------------------------------------------------
# Patch wlroots headers: replace C99 [static N] (invalid in C++) with [N].
# wlroots still uses this syntax as of 0.18 (it's a C library).
# ---------------------------------------------------------------------------
macro(patch_wlr_header _rel_path)
    set(_orig "")
    foreach(_inc_dir ${WLROOTS_INCLUDE_DIRS} /usr/include)
        if(EXISTS "${_inc_dir}/${_rel_path}")
            set(_orig "${_inc_dir}/${_rel_path}")
            break()
        endif()
    endforeach()
    if(_orig)
        get_filename_component(_dir "${CMAKE_BINARY_DIR}/wl_patched/${_rel_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${_dir}")
        file(READ "${_orig}" _content)
        string(REGEX REPLACE "\\[static [0-9]+" "[" _content "${_content}")
        file(WRITE "${CMAKE_BINARY_DIR}/wl_patched/${_rel_path}" "${_content}")
        message(STATUS "Patched ${_rel_path} → ${CMAKE_BINARY_DIR}/wl_patched/${_rel_path}")
    else()
        message(WARNING "patch_wlr_header: ${_rel_path} not found in include dirs")
    endif()
endmacro()

patch_wlr_header("wlr/types/wlr_scene.h")
patch_wlr_header("wlr/render/wlr_renderer.h")

# Detect whether wlr_scene::tree is a pointer or a value.
# This changed between wlroots versions.
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES ${CMAKE_BINARY_DIR}/wl_patched ${WLROOTS_INCLUDE_DIRS})
set(CMAKE_REQUIRED_DEFINITIONS -DWLR_USE_UNSTABLE)
check_c_source_compiles("
#include <wlr/types/wlr_scene.h>
int main(void) {
    struct wlr_scene *s = 0;
    struct wlr_scene_tree *t = s->tree;
    (void)t;
    return 0;
}
" WLR_SCENE_TREE_IS_POINTER)
if(WLR_SCENE_TREE_IS_POINTER)
    message(STATUS "wlr_scene::tree is a pointer")
    list(APPEND SIRENWM_BACKEND_COMPILE_DEFS WLR_SCENE_TREE_IS_POINTER)
else()
    message(STATUS "wlr_scene::tree is a value (using &scene_->tree)")
endif()

# Detect whether wlr_buffer_impl is public (0.17 style) or opaque (0.18+ style).
# Keep CMAKE_REQUIRED_DEFINITIONS from the previous probe (WLR_USE_UNSTABLE) and
# add the patched include path so the probe sees the same headers as the build.
set(CMAKE_REQUIRED_INCLUDES ${CMAKE_BINARY_DIR}/wl_patched ${WLROOTS_INCLUDE_DIRS})
set(CMAKE_REQUIRED_DEFINITIONS -DWLR_USE_UNSTABLE)
check_c_source_compiles("
#define WLR_USE_UNSTABLE
#include <wlr/types/wlr_buffer.h>
int main(void) {
    struct wlr_buffer_impl impl = {0};
    (void)impl;
    return 0;
}
" WLR_BUFFER_IMPL_PUBLIC)
if(WLR_BUFFER_IMPL_PUBLIC)
    message(STATUS "wlr_buffer_impl is public (0.17 style)")
else()
    message(STATUS "wlr_buffer_impl is opaque (0.18+ style) — using allocator+data_ptr path")
    list(APPEND SIRENWM_BACKEND_COMPILE_DEFS WLR_BUFFER_IMPL_OPAQUE)
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
    ${CMAKE_BINARY_DIR}/wl_patched    # patched wlr headers (C++ compatible)
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
