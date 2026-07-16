# Pure C++20 library + tool: self-play data generation and Multi-ProbCut coefficient fitting.
# Depends only on reversi::engine, same layering rule as cli/ and tools/wthor_extractor - no
# external data source here (unlike wthor_extractor), so both steps live in one tool.
add_library(mpc_fitter_lib STATIC
    src/dataset.cpp
)
target_include_directories(mpc_fitter_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mpc_fitter_lib PUBLIC reversi::engine)
target_link_libraries(mpc_fitter_lib PRIVATE reversi_warnings)

add_executable(mpc-fitter src/main.cpp)
target_link_libraries(mpc-fitter PRIVATE mpc_fitter_lib reversi::engine reversi_warnings)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(mpc_fitter_tests
    tests/dataset_test.cpp
)
target_link_libraries(mpc_fitter_tests PRIVATE mpc_fitter_lib reversi::engine
                                               GTest::gtest_main reversi_warnings)

include(GoogleTest)
gtest_discover_tests(mpc_fitter_tests DISCOVERY_MODE PRE_TEST)
