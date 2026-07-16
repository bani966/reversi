# Pure C++20 static library. Must never depend on Qt or any platform-specific API.
add_library(reversi_engine STATIC
    src/position.cpp
    src/moves.cpp
    src/perft.cpp
    src/eval.cpp
    src/tt.cpp
    src/shared_tt.cpp
    src/search.cpp
    src/solver.cpp
    src/pattern.cpp
    src/pattern_eval.cpp
    src/opening_book.cpp
    src/mpc.cpp
    src/move_selector.cpp
    src/analysis.cpp
    src/players.cpp
    src/selfplay.cpp
)
add_library(reversi::engine ALIAS reversi_engine)

target_include_directories(reversi_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(reversi_engine PRIVATE reversi_warnings)

# search.cpp spawns std::thread directly (searchLazySmp, M8) - Threads::Threads is the portable
# way to get pthread linked in on Linux/macOS too (same reasoning as app/CMakeLists.txt's own
# comment on GameController's worker thread). PUBLIC: reversi_engine is a static library, so the
# final executable (cli/tests/tools/app) is what actually needs pthread resolved at link time -
# this propagates that requirement to every consumer automatically.
find_package(Threads REQUIRED)
target_link_libraries(reversi_engine PUBLIC Threads::Threads)
