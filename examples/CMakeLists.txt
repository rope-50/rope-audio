# Example: load every WAV passed on the command line and play them at once,
# demonstrating the engine's simultaneous mixing.
add_executable(play_wav play_wav.cpp)
target_link_libraries(play_wav PRIVATE rope::audioengine)
