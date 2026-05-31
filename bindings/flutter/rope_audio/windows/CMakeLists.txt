# Flutter FFI plugin native build for Windows.
#
# Flutter invokes this file (add_subdirectory) while building the host app. The
# example app lives INSIDE the engine repo, so we must NOT add_subdirectory the
# repo root (that would re-process Flutter's own CMake). Instead we compile the
# rope core sources directly here, miniaudio-only, into rope_audio.dll and tell
# Flutter to bundle it next to the runner.
cmake_minimum_required(VERSION 3.14)
project(rope_audio_library LANGUAGES C CXX)  # C for stb_vorbis.c

include(FetchContent)

# Flutter symlinks the plugin under example/windows/flutter/ephemeral/.plugin_symlinks.
# Resolve that symlink to the real plugin dir FIRST (CMake collapses `..`
# lexically before following symlinks, so appending `..` to the symlinked path
# would escape via the wrong tree), THEN walk up to the repo root:
#   windows/ -> rope_audio/ -> flutter/ -> bindings/ -> <root>
# The plugin dir (one up from windows/) is the symlink; read its target to get
# the real plugin location, then walk up to the repo root:
#   rope_audio/ -> flutter/ -> bindings/ -> <root>
get_filename_component(_rope_link "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
if(IS_SYMLINK "${_rope_link}")
  file(READ_SYMLINK "${_rope_link}" _rope_plugin)
  file(TO_CMAKE_PATH "${_rope_plugin}" _rope_plugin)
  string(REGEX REPLACE "^/\\?\\?/" "" _rope_plugin "${_rope_plugin}") # strip Windows \??\ NT prefix
  if(NOT IS_ABSOLUTE "${_rope_plugin}")
    get_filename_component(_rope_plugin "${_rope_link}/../${_rope_plugin}" ABSOLUTE)
  endif()
else()
  set(_rope_plugin "${_rope_link}")
endif()
get_filename_component(ROPE_ROOT "${_rope_plugin}/../../.." ABSOLUTE)
message(STATUS "rope_audio: building engine core from ${ROPE_ROOT}")

# --- Header-only third-party deps (same versions as the main build) ---
FetchContent_Declare(dr_libs
  GIT_REPOSITORY https://github.com/mackron/dr_libs.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(dr_libs)

FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG        0.11.22
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  do-not-configure)   # fetch the header only; skip its CMake
FetchContent_MakeAvailable(miniaudio)

FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(stb)

# --- The plugin shared library (exports the rope_* C ABI) ---
# stb_vorbis.c is compiled into the same target (same CRT) so OGG's malloc/free
# pairing stays on one heap.
add_library(rope_audio SHARED
  "${ROPE_ROOT}/src/AudioEngine.cpp"
  "${ROPE_ROOT}/src/AudioBackend.cpp"
  "${ROPE_ROOT}/src/WavLoader.cpp"
  "${ROPE_ROOT}/src/rope_c.cpp"
  "${ROPE_ROOT}/src/backends/NullBackend.cpp"
  "${ROPE_ROOT}/src/backends/MiniaudioBackend.cpp"
  "${stb_SOURCE_DIR}/stb_vorbis.c"
)

target_include_directories(rope_audio PRIVATE
  "${ROPE_ROOT}/include"
  "${ROPE_ROOT}/src"
  "${dr_libs_SOURCE_DIR}"
  "${miniaudio_SOURCE_DIR}"
  "${stb_SOURCE_DIR}")

target_compile_features(rope_audio PRIVATE cxx_std_20)
target_compile_definitions(rope_audio PRIVATE
  ROPE_AUDIO_BACKEND_MINIAUDIO   # miniaudio only (no RtAudio/ASIO)
  ROPE_BUILD_SHARED ROPE_BUILD_LIBRARY)  # ROPE_API -> dllexport

if(MSVC)
  target_compile_options(rope_audio PRIVATE /utf-8)
endif()

# Produces rope_audio.dll; Dart opens it by that name.
set(rope_audio_bundled_libraries
  "$<TARGET_FILE:rope_audio>"
  PARENT_SCOPE
)
