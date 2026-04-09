find_package(ALSA QUIET)
if(NOT ALSA_FOUND)
    message(STATUS "ALSA not found — audio module disabled")
    return()
endif()

set(SOURCES audio_module.cpp)
set(LINK_LIBS ALSA::ALSA)
