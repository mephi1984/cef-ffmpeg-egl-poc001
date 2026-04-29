cmake_minimum_required(VERSION 3.21)
project(cef_egl_demo C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CEF expects find_package(CEF) to receive a CEF_ROOT pointing at the binary
# distribution. We bundle one under third_party/cef for convenience.
if(NOT DEFINED CEF_ROOT)
    set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef")
endif()
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# Build CEF's C++ wrapper from its bundled sources.
add_subdirectory("${CEF_ROOT}/libcef_dll" libcef_dll_wrapper)

# Define the imported libcef.so target. The full CEF source distribution
# does this in tests/shared but the "minimal" distribution does not, so we
# wire it up directly.
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

# System EGL / GLES2 / X11.
find_package(PkgConfig REQUIRED)
pkg_check_modules(EGL    REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLES2  REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(X11    REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale)

set(APP_NAME cef_egl_demo)
set(APP_SOURCES
    src/main.cpp
    src/browser_app.h
    src/browser_app.cpp
    src/browser_client.h
    src/browser_client.cpp
    src/render_handler.h
    src/render_handler.cpp
    src/gl_renderer.h
    src/gl_renderer.cpp
    src/gl_window.h
    src/gl_window.cpp
    src/video_decoder.h
    src/video_decoder.cpp
)

add_executable(${APP_NAME} ${APP_SOURCES})

# Pull in the CEF compiler/linker flags. SET_EXECUTABLE_TARGET_PROPERTIES is
# defined by cef_macros.cmake and applies the right -fPIC, visibility, RPATH,
# and libcef link options for the platform.
SET_EXECUTABLE_TARGET_PROPERTIES(${APP_NAME})

target_include_directories(${APP_NAME} PRIVATE
    "${CEF_ROOT}"               # for #include "include/cef_app.h"
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(${APP_NAME} PRIVATE
    libcef_lib                  # alias defined by CEF cmake; resolves to libcef.so
    libcef_dll_wrapper
    PkgConfig::EGL
    PkgConfig::GLES2
    PkgConfig::X11
    PkgConfig::FFMPEG
    pthread
    dl
)

# Make sure the runtime can find libcef.so sitting next to the binary.
set_target_properties(${APP_NAME} PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_RPATH   "$ORIGIN"
)

# Stage CEF's runtime files (libcef.so, *.pak, locales/, ICU, etc.) and
# our HTML assets next to the binary.
COPY_FILES("${APP_NAME}" "${CEF_BINARY_FILES}"  "${CEF_BINARY_DIR_RELEASE}" "${CMAKE_BINARY_DIR}")
COPY_FILES("${APP_NAME}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}"      "${CMAKE_BINARY_DIR}")

add_custom_command(TARGET ${APP_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/html"
            "${CMAKE_BINARY_DIR}/html"
    COMMENT "Staging html/ next to ${APP_NAME}"
)

# Stage the test video next to the binary if present, so the demo finds it
# via "<exe_dir>/testvideo001.mp4" without depending on cwd.
if(EXISTS "${CMAKE_SOURCE_DIR}/testvideo001.mp4")
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/testvideo001.mp4"
                "${CMAKE_BINARY_DIR}/testvideo001.mp4"
        COMMENT "Staging testvideo001.mp4 next to ${APP_NAME}"
    )
endif()

PRINT_CEF_CONFIG()
