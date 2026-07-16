# Pure C++20 library: .wtb parsing, move replay, and pattern-feature dataset extraction.
# Depends only on reversi::engine, same layering rule as cli/ - reuses Position/applyMove/
# legalMoves/pattern directly rather than reimplementing game rules for this pipeline.
add_library(wthor_extractor_lib STATIC
    src/parser.cpp
    src/dataset.cpp
    src/book.cpp
)
target_include_directories(wthor_extractor_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# PUBLIC: parser.hpp's public API uses reversi::Position directly, so consumers need engine/'s
# include dirs too.
target_link_libraries(wthor_extractor_lib PUBLIC reversi::engine)
target_link_libraries(wthor_extractor_lib PRIVATE reversi_warnings)

add_executable(wthor-extractor src/main.cpp)
target_link_libraries(wthor-extractor PRIVATE wthor_extractor_lib reversi::engine reversi_warnings)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(wthor_extractor_tests
    tests/parser_test.cpp
    tests/dataset_test.cpp
    tests/book_test.cpp
)
target_link_libraries(wthor_extractor_tests PRIVATE wthor_extractor_lib reversi::engine
                                                    GTest::gtest_main reversi_warnings)

include(GoogleTest)
gtest_discover_tests(wthor_extractor_tests DISCOVERY_MODE PRE_TEST)
