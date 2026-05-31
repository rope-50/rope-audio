# Example: load every WAV passed on the command line and play them at once,
# demonstrating the engine's simultaneous mixing.
add_executable(play_wav play_wav.cpp)
target_link_libraries(play_wav PRIVATE rope::audioengine)

# Smoke test for the C ABI (include/rope/rope.h). Pure C source, so it proves
# the ABI is C-callable; it links the C++ static lib, hence LINKER_LANGUAGE CXX.
add_executable(rope_c_smoke rope_c_smoke.c)
target_link_libraries(rope_c_smoke PRIVATE rope::audioengine)
set_target_properties(rope_c_smoke PROPERTIES LINKER_LANGUAGE CXX)

# Headless mixer throughput benchmark (Null backend; linear vs sinc, 1..64 voices).
add_executable(rope_bench rope_bench.cpp)
target_link_libraries(rope_bench PRIVATE rope::audioengine)
