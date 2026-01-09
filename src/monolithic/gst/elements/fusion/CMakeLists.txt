cmake_minimum_required(VERSION 3.5)

project(gstfusion)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST_BASE REQUIRED gstreamer-base-1.0>=1.16)
pkg_check_modules(GST REQUIRED gstreamer-1.0>=1.16)

add_library(gstfusion SHARED
    fusion.cpp
)

target_include_directories(gstfusion
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GST_BASE_INCLUDE_DIRS}
        ${GST_INCLUDE_DIRS}
)

target_link_libraries(gstfusion
    PRIVATE
        ${GST_BASE_LIBRARIES}
        ${GST_LIBRARIES}
)

install(TARGETS gstfusion
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/gstreamer-1.0
)
