find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON      REQUIRED xkbcommon)
pkg_check_modules(CAIRO          REQUIRED cairo)

list(APPEND SIRENWM_BACKEND_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/input_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/monitor_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/render_port.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/keyboard_port.cpp
)

list(APPEND SIRENWM_BACKEND_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../../libwl/include
    ${XKBCOMMON_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
)

list(APPEND SIRENWM_BACKEND_LINK_LIBS
    wlproto
    ${XKBCOMMON_LIBRARIES}
    ${CAIRO_LIBRARIES}
)
