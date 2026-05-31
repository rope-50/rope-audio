cmake_minimum_required(VERSION 3.24)

# Honor the MSVC_RUNTIME_LIBRARY abstraction (must be set before project()).
cmake_policy(SET CMP0091 NEW)

project(rope_audioengine
    VERSION 0.1.0
    DESCRIPTION "Modern C++ real-time audio engine for games and DAWs"
    LANGUAGES C CXX)  # C for stb_vorbis.c (OGG decoding)

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

# Use the dynamic CRT (/MD) for every target — including fetched static libs like
# stb_vorbis — so the whole module shares one CRT heap (avoids LNK4098 and any
# cross-CRT allocation hazard).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

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
set(ROPE_CORE_SOURCES
    src/AudioEngine.cpp
    src/AudioBackend.cpp
    src/WavLoader.cpp
    src/rope_c.cpp            # stable C ABI (include/rope/rope.h)
    src/backends/NullBackend.cpp
    src/backends/MiniaudioBackend.cpp
    src/backends/RtAudioBackend.cpp
)

# Fold stb_vorbis (OGG decoder) straight into the engine so the static library
# is self-contained. It is noisy old C, so silence its warnings per-source while
# our own code keeps /W4 -Wall.
list(APPEND ROPE_CORE_SOURCES ${ROPE_STB_VORBIS_SRC})
if(MSVC)
    set_source_files_properties(${ROPE_STB_VORBIS_SRC} PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${ROPE_STB_VORBIS_SRC} PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Apply the shared include paths, features, deps, backend selection and warnings
# to a target (used for both the static lib and the optional shared/FFI lib).
function(rope_configure_target tgt)
    target_include_directories(${tgt}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src       # for "backends/Backends.hpp"
            ${ROPE_DR_LIBS_INCLUDE_DIR}           # WAV/FLAC/MP3 (header-only)
            ${ROPE_STB_INCLUDE_DIR})              # OGG/Vorbis (header-only)
    target_compile_features(${tgt} PUBLIC cxx_std_20)
    # Decoder deps are header-only / folded in (see ROPE_CORE_SOURCES), so they
    # are build-time include dirs only — nothing leaks into the link interface.
    # Backends (each guarded by its own compile definition; the .cpp files
    # compile to nothing when their macro is undefined).
    if(ROPE_AUDIO_BACKEND_MINIAUDIO)
        target_compile_definitions(${tgt} PRIVATE ROPE_AUDIO_BACKEND_MINIAUDIO)
        target_include_directories(${tgt} PRIVATE ${ROPE_MINIAUDIO_INCLUDE_DIR})
        target_link_libraries(${tgt} PUBLIC ${ROPE_MINIAUDIO_SYSTEM_LIBS})
    endif()
    if(ROPE_AUDIO_BACKEND_RTAUDIO)
        target_compile_definitions(${tgt} PRIVATE ROPE_AUDIO_BACKEND_RTAUDIO)
        target_link_libraries(${tgt} PRIVATE RtAudio::rtaudio)
    endif()
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(rope_audioengine ${ROPE_CORE_SOURCES})
add_library(rope::audioengine ALIAS rope_audioengine)
rope_configure_target(rope_audioengine)
set_target_properties(rope_audioengine PROPERTIES EXPORT_NAME audioengine)

# Optional shared library exposing the C ABI with exported symbols — this is the
# artifact FFI hosts (e.g. the Flutter plugin) load via dlopen/LoadLibrary.
option(ROPE_AUDIO_BUILD_SHARED "Build a shared library exposing the C ABI (for FFI)" OFF)
if(ROPE_AUDIO_BUILD_SHARED)
    add_library(rope_audio_shared SHARED ${ROPE_CORE_SOURCES})
    rope_configure_target(rope_audio_shared)
    # ROPE_API -> __declspec(dllexport) for the rope_* C symbols on Windows.
    target_compile_definitions(rope_audio_shared PRIVATE ROPE_BUILD_SHARED ROPE_BUILD_LIBRARY)
    set_target_properties(rope_audio_shared PROPERTIES OUTPUT_NAME rope_audio)
endif()

# ---------------------------------------------------------------------------
# Install + find_package(rope) support (opt-in). Produces a self-contained,
# relocatable package: `find_package(rope CONFIG REQUIRED)` gives the imported
# target rope::audioengine. Use a miniaudio-only build (RtAudio is a separate
# archive that isn't packaged here): -DROPE_AUDIO_BACKEND_RTAUDIO=OFF.
# Consumers may instead vendor via FetchContent + add_subdirectory (no install).
# ---------------------------------------------------------------------------
option(ROPE_AUDIO_INSTALL "Generate install + find_package(rope) rules" OFF)
if(ROPE_AUDIO_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS rope_audioengine
        EXPORT  ropeTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/rope DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT ropeTargets
        FILE      ropeTargets.cmake
        NAMESPACE rope::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rope)

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/ropeConfigVersion.cmake
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ropeConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/ropeConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rope)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ropeConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ropeConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rope)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(ROPE_AUDIO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Tests (GoogleTest + CTest). Run with `ctest --test-dir build/<preset>`.
# ---------------------------------------------------------------------------
option(ROPE_AUDIO_BUILD_TESTS "Build the unit test suite" ON)
if(ROPE_AUDIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "rope_audioengine ${PROJECT_VERSION} configured "
               "(examples: ${ROPE_AUDIO_BUILD_EXAMPLES}, "
               "miniaudio: ${ROPE_AUDIO_BACKEND_MINIAUDIO}, "
               "rtaudio: ${ROPE_AUDIO_BACKEND_RTAUDIO}, "
               "asio: ${ROPE_AUDIO_RTAUDIO_ASIO})")
