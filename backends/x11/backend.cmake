list(APPEND SIRENWM_BACKEND_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/x11_backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/adopt.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/events.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ewmh.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/input_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/keyboard_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/loop.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/render_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/monitor_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/tray_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/xconn.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gl_port.cpp
)

list(APPEND SIRENWM_BACKEND_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CAIRO_INCLUDE_DIRS}
    ${CAIROXCB_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
    ${LIBPNG_INCLUDE_DIRS}
    ${XCBKEYSYMS_INCLUDE_DIRS}
)

find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)

list(APPEND SIRENWM_BACKEND_LINK_LIBS
    X11 X11-xcb xcb xcb-randr xkbfile Xfixes
    ${CAIRO_LIBRARIES}
    ${CAIROXCB_LIBRARIES}
    ${XCBKEYSYMS_LIBRARIES}
    xkbcommon
    OpenGL::GL OpenGL::EGL
)
