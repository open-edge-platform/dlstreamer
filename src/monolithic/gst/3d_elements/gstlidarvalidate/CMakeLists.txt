cmake_minimum_required(VERSION 3.5)

project(g3dlidarvalidate)

add_library(g3dlidarvalidate STATIC
    g3dlidarvalidate.cpp
)
set_compile_flags(g3dlidarvalidate)

target_include_directories(g3dlidarvalidate
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(g3dlidarvalidate
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        dlstreamer_gst_meta
)
