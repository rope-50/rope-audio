# Third-party notices

rope-audioengine itself is MIT licensed (see [LICENSE](LICENSE)). It builds on
the following third-party components, all under permissive licenses. Each is
fetched from source at configure time (CMake `FetchContent`); none is modified.

| Component | Used for | License | Distributed in binaries |
|-----------|----------|---------|--------------------------|
| [dr_libs](https://github.com/mackron/dr_libs) (`dr_wav`, `dr_flac`, `dr_mp3`) | WAV / FLAC / MP3 decoding | Public domain **or** MIT-0 (dual) | Yes (compiled into the engine) |
| [miniaudio](https://github.com/mackron/miniaudio) | Cross-platform audio device backend | Public domain **or** MIT-0 (dual) | Yes (compiled into the engine) |
| [stb_vorbis](https://github.com/nothings/stb) | OGG/Vorbis decoding | Public domain **or** MIT (dual) | Yes (compiled into the engine) |
| [RtAudio](https://github.com/thestk/rtaudio) | Optional desktop backend (ASIO on Windows) | MIT-style (RtAudio license) | Only if the RtAudio backend is enabled |
| [GoogleTest](https://github.com/google/googletest) | Unit tests | BSD-3-Clause | **No** (test-only; never shipped) |

## License texts

The full upstream license text for each component is distributed with its
source (e.g. the header comment in `dr_wav.h` / `miniaudio.h` / `stb_vorbis.c`,
and `RtAudio`'s `LICENSE`). Because these are fetched at build time, the exact
text travels with the dependency cache rather than being vendored here.

- **dr_libs / miniaudio**: choose either Public Domain (Unlicense) or MIT-0.
  No attribution required, but it is appreciated.
- **stb_vorbis**: Public Domain (Unlicense) or MIT.
- **RtAudio**: permissive MIT-style license; asks that the copyright notice and
  a note of any changes be retained. rope does not modify RtAudio sources.
- **GoogleTest**: BSD-3-Clause. Used only to build and run the test suite; it is
  not part of the engine library, the C ABI, or any shipped binding.

If you ship a binary built with the RtAudio backend enabled, include RtAudio's
copyright notice. With the default (miniaudio-only) build there is no
attribution obligation, though crediting the projects above is welcome.
