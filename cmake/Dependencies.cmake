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

# Header-only: WAV/FLAC/MP3 implementations are compiled inside our own TUs, so
# the engine target just needs the include dir at build time (not as a link dep —
# keeping it out of the static lib's link interface makes install/export clean).
set(ROPE_DR_LIBS_INCLUDE_DIR "${dr_libs_SOURCE_DIR}" CACHE INTERNAL "rope: dr_libs include dir")

# --- stb_vorbis: OGG/Vorbis decoder ----------------------------------------
# dr_libs covers WAV/FLAC/MP3 but not Vorbis. stb_vorbis.c is folded directly
# into the engine target (see the top-level CMakeLists, warnings silenced) so the
# static library is self-contained for install/find_package. WavLoader.cpp also
# includes it header-only for the prototypes via this include dir.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

set(ROPE_STB_VORBIS_SRC  "${stb_SOURCE_DIR}/stb_vorbis.c" CACHE INTERNAL "rope: stb_vorbis source")
set(ROPE_STB_INCLUDE_DIR "${stb_SOURCE_DIR}"              CACHE INTERNAL "rope: stb include dir")

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

    set(ROPE_MINIAUDIO_INCLUDE_DIR "${miniaudio_SOURCE_DIR}" CACHE INTERNAL "rope: miniaudio include dir")

    # miniaudio's runtime needs a few system libs on POSIX platforms. These are
    # linked PUBLICly on the engine target so a consumer of the (static) library
    # pulls them too; the generated package config find_dependency()s Threads.
    if(ANDROID)
        set(ROPE_MINIAUDIO_SYSTEM_LIBS OpenSLES log android CACHE INTERNAL "rope: miniaudio sys libs")
    elseif(APPLE)
        set(ROPE_MINIAUDIO_SYSTEM_LIBS "" CACHE INTERNAL "rope: miniaudio sys libs") # frameworks via pragmas
    elseif(UNIX)
        find_package(Threads REQUIRED)
        set(ROPE_MINIAUDIO_SYSTEM_LIBS Threads::Threads ${CMAKE_DL_LIBS} m CACHE INTERNAL "rope: miniaudio sys libs")
    else()
        set(ROPE_MINIAUDIO_SYSTEM_LIBS "" CACHE INTERNAL "rope: miniaudio sys libs")
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
