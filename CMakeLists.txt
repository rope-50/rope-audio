cmake_minimum_required(VERSION 3.24)

project(rope_audioengine
    VERSION 0.1.0
    DESCRIPTION "Modern C++ real-time audio engine for games and DAWs"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Project-wide settings
# ---------------------------------------------------------------------------
# Default to Release for single-config generators (Ninja, Makefiles).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Predictable output location for binaries.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ROPE_AUDIO_BUILD_EXAMPLES "Build example applications" ON)

# Backend selection. miniaudio is the cross-platform default; RtAudio is a
# desktop-only backend kept around mainly for ASIO on Windows.
option(ROPE_AUDIO_BACKEND_MINIAUDIO "Build the miniaudio backend (cross-platform)" ON)

if(ANDROID OR IOS)
    set(_rope_rtaudio_default OFF)   # RtAudio has no mobile support
else()
    set(_rope_rtaudio_default ON)
endif()
option(ROPE_AUDIO_BACKEND_RTAUDIO "Build the RtAudio backend (desktop)" ${_rope_rtaudio_default})

# Enable ASIO inside RtAudio (Windows-only, self-contained — RtAudio bundles the
# ASIO SDK). On by default on Windows so the pro/DAW path is ready to use.
if(WIN32)
    set(_rope_asio_default ON)
else()
    set(_rope_asio_default OFF)
endif()
option(ROPE_AUDIO_RTAUDIO_ASIO "Enable ASIO support in the RtAudio backend (Windows)" ${_rope_asio_default})

if(NOT ROPE_AUDIO_BACKEND_MINIAUDIO AND NOT ROPE_AUDIO_BACKEND_RTAUDIO)
    message(FATAL_ERROR "At least one audio backend must be enabled "
                        "(ROPE_AUDIO_BACKEND_MINIAUDIO / ROPE_AUDIO_BACKEND_RTAUDIO).")
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies (dr_wav, miniaudio, RtAudio)
# ---------------------------------------------------------------------------
include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# The engine library
# ---------------------------------------------------------------------------
add_library(rope_audioengine
    src/AudioEngine.cpp
    src/AudioBackend.cpp
    src/WavLoader.cpp
    src/rope_c.cpp            # stable C ABI (include/rope/rope.h)
    src/backends/MiniaudioBackend.cpp
    src/backends/RtAudioBackend.cpp
)
add_library(rope::audioengine ALIAS rope_audioengine)

target_include_directories(rope_audioengine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src      # for "backends/Backends.hpp"
)

target_compile_features(rope_audioengine PUBLIC cxx_std_20)

# WAV decoding (always on).
target_link_libraries(rope_audioengine PRIVATE dr_libs)

# Backends (each guarded by its own compile definition; the .cpp files compile
# to nothing when their macro is undefined).
if(ROPE_AUDIO_BACKEND_MINIAUDIO)
    target_compile_definitions(rope_audioengine PRIVATE ROPE_AUDIO_BACKEND_MINIAUDIO)
    target_link_libraries(rope_audioengine PRIVATE miniaudio_header)
endif()
if(ROPE_AUDIO_BACKEND_RTAUDIO)
    target_compile_definitions(rope_audioengine PRIVATE ROPE_AUDIO_BACKEND_RTAUDIO)
    target_link_libraries(rope_audioengine PRIVATE RtAudio::rtaudio)
endif()

if(MSVC)
    target_compile_options(rope_audioengine PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(rope_audioengine PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(ROPE_AUDIO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

message(STATUS "rope_audioengine ${PROJECT_VERSION} configured "
               "(examples: ${ROPE_AUDIO_BUILD_EXAMPLES}, "
               "miniaudio: ${ROPE_AUDIO_BACKEND_MINIAUDIO}, "
               "rtaudio: ${ROPE_AUDIO_BACKEND_RTAUDIO}, "
               "asio: ${ROPE_AUDIO_RTAUDIO_ASIO})")
