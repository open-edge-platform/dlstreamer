cmake_minimum_required(VERSION 3.5)

project(gstlidarparse)


add_library(gstlidarparse SHARED
    gstlidarparse.cpp
    gstlidarmeta.cpp
)

target_include_directories(gstlidarparse
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(gstlidarparse
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
)

install(TARGETS gstlidarparse
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/gstreamer-1.0
)
