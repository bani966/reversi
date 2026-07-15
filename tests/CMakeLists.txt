include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
)
# Match the dynamic CRT our targets use on MSVC.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(engine_tests
    engine/position_test.cpp
    engine/naive_reference_test.cpp
    engine/moves_test.cpp
    engine/differential_test.cpp
    engine/perft_test.cpp
    engine/eval_test.cpp
    engine/search_test.cpp
    support/naive_reference.cpp
)
target_link_libraries(engine_tests PRIVATE reversi::engine GTest::gtest_main reversi_warnings)

include(GoogleTest)
# PRE_TEST discovery plays well with the multi-config Visual Studio generator.
gtest_discover_tests(engine_tests DISCOVERY_MODE PRE_TEST)
