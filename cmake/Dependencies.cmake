# ---------------------------------------------------------------------------
# Third-party dependencies, fetched and built from source at configure time.
# Keeping them as FetchContent means a fresh clone of this repo builds with a
# single `cmake` invocation — no submodules, no system packages.
# ---------------------------------------------------------------------------
include(FetchContent)

# --- RtAudio: cross-platform real-time audio I/O ---------------------------
# Windows: WASAPI + DirectSound (ASIO needs the proprietary SDK, off by default)
# macOS:   CoreAudio
# Linux:   ALSA / PulseAudio / JACK
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)         # static-link RtAudio into the engine
set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)     # skip RtAudio's own test/demo apps
set(RTAUDIO_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    rtaudio
    GIT_REPOSITORY https://github.com/thestk/rtaudio.git
    GIT_TAG        6.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(rtaudio)

# In the FetchContent build tree RtAudio only defines the plain `rtaudio`
# target; the namespaced `RtAudio::rtaudio` alias exists only in its installed
# package config. Provide it here so the rest of the build can use the same
# name whether RtAudio is fetched or found via find_package().
if(NOT TARGET RtAudio::rtaudio)
    add_library(RtAudio::rtaudio ALIAS rtaudio)
endif()

# --- dr_wav: single-header WAV decoder -------------------------------------
# We expose the header via an INTERFACE target; the implementation is compiled
# exactly once (DR_WAV_IMPLEMENTATION is defined in src/WavLoader.cpp).
# NOTE: pin GIT_TAG to a commit hash for fully reproducible builds.
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(dr_libs)

add_library(dr_libs INTERFACE)
target_include_directories(dr_libs INTERFACE ${dr_libs_SOURCE_DIR})
