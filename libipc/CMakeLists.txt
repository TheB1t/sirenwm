add_library(sirenwm_ipc STATIC
    src/channel.cpp
    src/endpoint.cpp
    src/shared_buffer.cpp
)

target_include_directories(sirenwm_ipc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
