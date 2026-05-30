# ---------------------------------------------------------------------------
# Third-party dependencies, fetched and built from source at configure time.
# Keeping them as FetchContent means a fresh clone of this repo builds with a
# single `cmake` invocation — no submodules, no system packages.
# ---------------------------------------------------------------------------
include(FetchContent)

# --- dr_wav: single-header WAV decoder -------------------------------------
# Exposed via an INTERFACE target; the implementation is compiled exactly once
# (DR_WAV_IMPLEMENTATION is defined in src/WavLoader.cpp).
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

# --- miniaudio: cross-platform audio device backend (DEFAULT) --------------
# Covers Windows (WASAPI/DSound), macOS + iOS (CoreAudio), Linux (ALSA/Pulse/
# JACK) and Android (AAudio/OpenSL ES) — i.e. every target platform. Single
# header; the implementation is compiled in src/backends/MiniaudioBackend.cpp.
if(ROPE_AUDIO_BACKEND_MINIAUDIO)
    # miniaudio ships its own CMakeLists (a prebuilt library + vorbis/opus/SDL2
    # examples) that we don't want. Point SOURCE_SUBDIR at a non-existent dir so
    # FetchContent populates the source but skips add_subdirectory — we just need
    # the single header, compiled with our own MA_NO_* config in
    # src/backends/MiniaudioBackend.cpp.
    FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG        0.11.22
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  do-not-configure
    )
    FetchContent_MakeAvailable(miniaudio)

    add_library(miniaudio_header INTERFACE)
    target_include_directories(miniaudio_header INTERFACE ${miniaudio_SOURCE_DIR})

    # miniaudio's runtime needs a few system libs on POSIX platforms.
    if(ANDROID)
        target_link_libraries(miniaudio_header INTERFACE OpenSLES log android)
    elseif(APPLE)
        # CoreAudio/AudioToolbox are pulled in by miniaudio via framework pragmas.
    elseif(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(miniaudio_header INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
    endif()
endif()

# --- RtAudio: desktop backend, primarily for ASIO on Windows ---------------
# miniaudio does not support ASIO (Steinberg licensing). RtAudio does and even
# bundles the ASIO SDK sources, so enabling it gives low-latency pro audio for
# the DAW use case. Built static and linked into the engine.
if(ROPE_AUDIO_BACKEND_RTAUDIO)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)

    if(ROPE_AUDIO_RTAUDIO_ASIO)
        set(RTAUDIO_API_ASIO ON CACHE BOOL "" FORCE)  # self-contained; no external SDK
    endif()

    FetchContent_Declare(
        rtaudio
        GIT_REPOSITORY https://github.com/thestk/rtaudio.git
        GIT_TAG        6.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(rtaudio)

    # The FetchContent tree only defines the plain `rtaudio` target; the
    # namespaced alias exists only in RtAudio's installed package config.
    if(NOT TARGET RtAudio::rtaudio)
        add_library(RtAudio::rtaudio ALIAS rtaudio)
    endif()
endif()
