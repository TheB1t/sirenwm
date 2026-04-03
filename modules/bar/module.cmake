find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(PANGO REQUIRED pangocairo)

set(SOURCES
    bar_module.cpp
    bar_module_core.cpp
    widgets/bar_widgets.cpp
)
set(LINK_LIBS
    ${CAIRO_LIBRARIES}
    ${PANGO_LIBRARIES}
    fontconfig
)
set(INCLUDE_DIRS
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
)
