cmake_minimum_required(VERSION 3.5)

project(g3dlidarparse)

add_library(g3dlidarparse STATIC
    g3dlidarparse.cpp
    ${CMAKE_SOURCE_DIR}/src/gst/metadata/g3dlidarmeta.cpp
)
set_compile_flags(g3dlidarparse)

target_include_directories(g3dlidarparse
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(g3dlidarparse
    PUBLIC
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
)
