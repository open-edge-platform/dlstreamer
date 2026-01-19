cmake_minimum_required(VERSION 3.5)

project(g3dlidarparse)


add_library(g3dlidarparse SHARED
    g3dlidarparse.cpp
    ${CMAKE_SOURCE_DIR}/src/gst/metadata/g3dlidarmeta.cpp
)

target_include_directories(g3dlidarparse
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(g3dlidarparse
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
)

install(TARGETS g3dlidarparse
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/gstreamer-1.0
)
