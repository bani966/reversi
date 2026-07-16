# Qt GUI. Qt types must never leak into engine/.
# Multimedia is linked from M0 so the toolchain (and the aqt qtmultimedia module
# install) is verified long before QSoundEffect is actually used. Core is listed explicitly
# (already pulled in transitively via Widgets) so app_tests below can depend on just "Core" being
# requested, not on Widgets' transitive reach.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia)
qt_standard_project_setup()

# GameController runs AI search on a std::thread; MSVC needs nothing extra, but Threads::Threads
# is the portable way to get pthread linked in on Linux/macOS toolchains too.
find_package(Threads REQUIRED)

qt_add_executable(reversi-app
    src/main.cpp
    src/MainWindow.cpp
    src/BoardWidget.cpp
    src/GameController.cpp
    src/GameNotation.cpp
    src/SettingsDialog.cpp
    src/TitleBarWidget.cpp
    src/Palette.cpp
)

target_link_libraries(reversi-app PRIVATE
    reversi::engine
    Qt6::Widgets
    Qt6::Multimedia
    Threads::Threads
    reversi_warnings
)

# Windows 11 DWM corner-rounding spike (MainWindow.cpp, Q_OS_WIN-guarded) - no-op on other
# platforms since dwmapi is Windows-only.
if(WIN32)
    target_link_libraries(reversi-app PRIVATE dwmapi)
endif()

set_target_properties(reversi-app PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# app_tests (M9 phase 2): GameNotation's pure JSON/transcript/board-string logic, tested
# independently of the GUI - Qt6::Core only (no Widgets, no QApplication), so this binary has no
# display-server dependency and stays fast. Own small test target, following the same "one test
# binary per component" precedent as tools/wthor_extractor's and tools/mpc_fitter's own test
# executables (each declares GoogleTest independently via FetchContent - see e.g.
# tools/mpc_fitter/CMakeLists.txt - a standard, idempotent pattern, not a new risk) - not folded
# into tests/engine_tests, which stays completely untouched. Naturally skipped on CI
# configurations that set REVERSI_BUILD_APP=OFF (ci-linux and everything that inherits from it -
# see CMakePresets.json), since this whole file isn't entered there; no separate
# display-server-avoidance logic needed. Also gated on REVERSI_BUILD_TESTS, matching that flag's
# documented purpose ("Build unit tests") - the top-level CMakeLists.txt calls
# add_subdirectory(app) after enable_testing() has already run for exactly this reason.
if(REVERSI_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(app_tests
        src/GameNotation.cpp
        tests/game_notation_test.cpp
    )
    target_include_directories(app_tests PRIVATE src)
    target_link_libraries(app_tests PRIVATE reversi::engine Qt6::Core GTest::gtest_main
                                            reversi_warnings)

    include(GoogleTest)
    gtest_discover_tests(app_tests DISCOVERY_MODE PRE_TEST)
endif()
