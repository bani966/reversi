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
    engine/search_iterative_test.cpp
    engine/tt_test.cpp
    engine/ordering_test.cpp
    engine/aspiration_test.cpp
    engine/exit_criterion_m4_test.cpp
    engine/solver_test.cpp
    engine/solver_ordering_test.cpp
    engine/solver_tt_test.cpp
    engine/ffo_test.cpp
    engine/pattern_test.cpp
    engine/pattern_eval_test.cpp
    engine/opening_book_test.cpp
    engine/exit_criterion_m6_test.cpp
    engine/players_test.cpp
    engine/selfplay_test.cpp
    support/baseline_search.cpp
    support/baseline_solver.cpp
    support/benchmark_positions.cpp
    support/endgame_positions.cpp
    support/ffo.cpp
    support/naive_reference.cpp
    support/search_checks.cpp
)
target_link_libraries(engine_tests PRIVATE reversi::engine GTest::gtest_main reversi_warnings)
# Absolute path to tests/data, baked in at configure time so ffo_test.cpp can locate the
# vendored FFO data file regardless of ctest's working directory (which varies by generator/
# platform, unlike the source tree layout).
target_compile_definitions(engine_tests PRIVATE
    "REVERSI_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")

include(GoogleTest)
# PRE_TEST discovery plays well with the multi-config Visual Studio generator.
gtest_discover_tests(engine_tests DISCOVERY_MODE PRE_TEST)
