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

option(ROPE_AUDIO_BUILD_EXAMPLES "Build example applications" ON)

# ---------------------------------------------------------------------------
# Third-party dependencies (RtAudio, dr_wav)
# ---------------------------------------------------------------------------
include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# The engine library
# ---------------------------------------------------------------------------
add_library(rope_audioengine
    src/AudioEngine.cpp
    src/WavLoader.cpp
)
add_library(rope::audioengine ALIAS rope_audioengine)

target_include_directories(rope_audioengine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(rope_audioengine PUBLIC cxx_std_20)

target_link_libraries(rope_audioengine
    PRIVATE
        RtAudio::rtaudio   # kept behind the pimpl; not part of the public API.
        dr_libs            # CMake still links both into consumers of this
                           # static lib via the LINK_ONLY interface.
)

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
               "(examples: ${ROPE_AUDIO_BUILD_EXAMPLES})")
