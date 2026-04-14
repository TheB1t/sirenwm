list(APPEND SIRENWM_BACKEND_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/backend/x11_backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/backend/adopt.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/backend/events.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/backend/ewmh.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/backend/loop.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ports/input_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ports/keyboard_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ports/render_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ports/monitor_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/x11/tray_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/x11/xconn.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/x11/x11_atoms.cpp
)

list(APPEND SIRENWM_BACKEND_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CAIRO_INCLUDE_DIRS}
    ${CAIROXCB_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${LIBPNG_INCLUDE_DIRS}
    ${XCBKEYSYMS_INCLUDE_DIRS}
)

list(APPEND SIRENWM_BACKEND_LINK_LIBS
    xcb_wrappers
    X11 X11-xcb xcb xcb-randr xkbfile Xfixes
    ${CAIRO_LIBRARIES}
    ${CAIROXCB_LIBRARIES}
    ${XCBKEYSYMS_LIBRARIES}
    xkbcommon
)

if(SIRENWM_DEBUG_UI)
    find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
    list(APPEND SIRENWM_BACKEND_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/ports/gl_port.cpp)
    list(APPEND SIRENWM_BACKEND_LINK_LIBS OpenGL::GL OpenGL::EGL)
endif()
