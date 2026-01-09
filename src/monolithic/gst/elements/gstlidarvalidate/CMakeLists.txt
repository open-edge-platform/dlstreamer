cmake_minimum_required(VERSION 3.5)

project(gstlidarvalidate)

add_library(gstlidarvalidate SHARED
    gstlidarvalidate.cpp
    gstlidarmeta.cpp
)

target_include_directories(gstlidarvalidate
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(gstlidarvalidate
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
)

install(TARGETS gstlidarvalidate
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/gstreamer-1.0
)
