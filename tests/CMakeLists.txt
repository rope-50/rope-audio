# Unit tests (GoogleTest, fetched from source). Run with `ctest`.
include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE)
# On Windows, link GoogleTest against the dynamic CRT (/MD) to match rope.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(rope_tests
    test_decode.cpp
    test_engine.cpp
    test_c_abi.cpp
)
target_link_libraries(rope_tests PRIVATE rope::audioengine GTest::gtest_main)
target_include_directories(rope_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(rope_tests)
