find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON      REQUIRED xkbcommon)
pkg_check_modules(CAIRO          REQUIRED cairo)

list(APPEND SIRENWM_BACKEND_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/backend/display_server_backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ports/display_server_input_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ports/display_server_monitor_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ports/display_server_render_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ports/display_server_keyboard_port.cpp
)

list(APPEND SIRENWM_BACKEND_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${XKBCOMMON_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
)

list(APPEND SIRENWM_BACKEND_LINK_LIBS
    sirenwm_ipc
    ${XKBCOMMON_LIBRARIES}
    ${CAIRO_LIBRARIES}
)
