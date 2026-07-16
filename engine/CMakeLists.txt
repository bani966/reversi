# Pure C++20 static library. Must never depend on Qt or any platform-specific API.
add_library(reversi_engine STATIC
    src/position.cpp
    src/moves.cpp
    src/perft.cpp
    src/eval.cpp
    src/tt.cpp
    src/search.cpp
    src/solver.cpp
    src/pattern.cpp
    src/players.cpp
    src/selfplay.cpp
)
add_library(reversi::engine ALIAS reversi_engine)

target_include_directories(reversi_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(reversi_engine PRIVATE reversi_warnings)
