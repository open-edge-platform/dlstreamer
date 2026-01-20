cmake_minimum_required(VERSION 3.5)

project(g3dlidarvalidate)

add_library(g3dlidarvalidate SHARED
    g3dlidarvalidate.cpp
)

target_include_directories(g3dlidarvalidate
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(g3dlidarvalidate
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
    dlstreamer_gst_meta
)

install(TARGETS g3dlidarvalidate
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/gstreamer-1.0
)
