vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO opencv/opencv
    REF "${VERSION}"
    SHA512 e90c75b1ce8332639c853e14a59059ec9b89ffa1d5435ab59820f4fa2e12d30846592672af808817d542a91248b164f6d54eeb6331be52e48062113f24d4c835
    HEAD_REF 5.x
    PATCHES
        0001-vcpkg-install-layout.patch
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        "geometry"   BUILD_opencv_geometry
        "intrinsics" CV_ENABLE_INTRINSICS
        "jpeg"       WITH_JPEG
        "opencl"     WITH_OPENCL
        "png"        WITH_PNG
        "thread"     OPENCV_ENABLE_THREAD_SUPPORT
        "win32ui"    WITH_WIN32UI
        "world"      BUILD_opencv_world
    INVERTED_FEATURES
        "fs"         OPENCV_DISABLE_FILESYSTEM_SUPPORT
)

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(BUILD_SHARED_LIBS OFF)
else()
    set(BUILD_SHARED_LIBS ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
        -DCMAKE_CXX_STANDARD=17
        -DBUILD_TESTS=OFF
        -DBUILD_PERF_TESTS=OFF
        -DBUILD_EXAMPLES=OFF
        -DBUILD_opencv_apps=OFF
        -DBUILD_opencv_dnn=OFF
        -DBUILD_opencv_python3=OFF
        -DBUILD_JPEG=OFF
        -DBUILD_PNG=OFF
        -DBUILD_ZLIB=OFF
        -DOpenCV_INSTALL_BINARIES_PREFIX=
        -DOPENCV_BIN_INSTALL_PATH=bin
        -DWITH_FFMPEG=OFF
        -DWITH_IPP=OFF
        -DWITH_OPENEXR=OFF
        -DWITH_VA=OFF
        -DWITH_VA_INTEL=OFF
        -DINSTALL_TO_MANGLED_PATHS=OFF
        -DOPENCV_CONFIG_INSTALL_PATH=share/opencv5
        -DOPENCV_INCLUDE_INSTALL_PATH=include/opencv5
        -DOPENCV_LIB_INSTALL_PATH=lib
        -DOPENCV_OTHER_INSTALL_PATH=share/opencv5
        -DOPENCV_GENERATE_PKGCONFIG=ON
        -DOPENCV_GENERATE_SETUPVARS=OFF
        ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH share/opencv5)
vcpkg_copy_pdbs()

if("opencl" IN_LIST FEATURES)
    file(APPEND "${CURRENT_PACKAGES_DIR}/share/opencv5/OpenCVConfig.cmake"
        "\ninclude(CMakeFindDependencyMacro)\nfind_dependency(OpenCL CONFIG)\n")
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
