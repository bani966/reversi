# Console harness over the engine. Depends only on reversi::engine.
add_executable(reversi-cli src/main.cpp)

target_link_libraries(reversi-cli PRIVATE reversi::engine reversi_warnings)
target_compile_definitions(reversi-cli PRIVATE REVERSI_VERSION="${PROJECT_VERSION}")
