# Standalone consumer that links rope via find_package(rope) against an installed
# tree. Built by CI (and locally) to verify the install + exported package works
# end to end. This is intentionally NOT part of the main build.
cmake_minimum_required(VERSION 3.24)
project(rope_consumer LANGUAGES CXX)

find_package(rope CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE rope::audioengine)
