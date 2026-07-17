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

# App icon (M10 phase 5): a green board tile with a black/white split disc, using this app's own
# real board-green/disc-fill colors (BoardWidget.cpp's boardPalette(), dark theme values) rather
# than an invented palette - generated once via Pillow, committed as plain binary assets (assets/
# reversi.ico, assets/reversi.icns), same "generated asset, not a build-time dependency" precedent
# as tests/data/'s fixtures. Windows gets it via a compiled-in .rc resource (win_resources.rc,
# below); macOS via MACOSX_BUNDLE_ICON_FILE + a bundle resource, set alongside the other
# set_target_properties() call further down.
if(WIN32)
    target_sources(reversi-app PRIVATE win_resources.rc)
elseif(APPLE)
    set(REVERSI_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/reversi.icns")
    target_sources(reversi-app PRIVATE "${REVERSI_ICNS}")
    set_source_files_properties("${REVERSI_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

target_link_libraries(reversi-app PRIVATE
    reversi::engine
    Qt6::Widgets
    Qt6::Multimedia
    Threads::Threads
    reversi_warnings
)

# M10 phase 2: the move-placement sound (GameController.cpp) started out embedded via Qt's
# resource system (qt_add_resources), but QSoundEffect on this machine's Multimedia backend
# reliably failed to load ANY qrc: source (status went straight to Error for both a plain WAV and
# an MP3), while the exact same files loaded and played fine from a real filesystem path -
# confirmed by direct testing, not assumed. Copying the asset next to the executable instead
# (resolved at runtime via QCoreApplication::applicationDirPath(), see GameController.cpp) works
# around that limitation. copy_if_different, not a plain copy, so incremental rebuilds don't
# rewrite the file (and bump its timestamp) when the source asset hasn't changed.
# WAV, not MP3: QSoundEffect is documented as a short-low-latency-effect player, and in practice
# on this machine it played a WAV reliably but never got past a failed MP3 load - confirmed by
# testing both, not assumed either.
add_custom_command(TARGET reversi-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets/move-self.wav"
        "$<TARGET_FILE_DIR:reversi-app>/move-self.wav"
    COMMENT "Copying move-placement sound asset next to reversi-app"
)

# Windows 11 DWM corner-rounding spike (MainWindow.cpp, Q_OS_WIN-guarded) - no-op on other
# platforms since dwmapi is Windows-only.
if(WIN32)
    target_link_libraries(reversi-app PRIVATE dwmapi)
endif()

set_target_properties(reversi-app PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_ICON_FILE reversi.icns
    # CFBundleName - without this it defaults to the CMake target name ("reversi-app"), which is
    # what actually showed up as the app's displayed name (menu bar, Force Quit, Spotlight) before
    # this was set; discovered only once someone actually launched a packaged build, since the
    # dev workflow never surfaces Info.plist's contents.
    MACOSX_BUNDLE_BUNDLE_NAME "Reversi"
)

# Also rename the bundle/executable itself on macOS ("Reversi.app", Contents/MacOS/Reversi) rather
# than just the Info.plist's internal name above - Finder's Applications-folder label and the DMG
# volume come from this, not from MACOSX_BUNDLE_BUNDLE_NAME. Windows-only reversi-app.exe naming
# (win_resources.rc, packaging/windows/reversi.iss.in, CI) is untouched by keeping this Apple-only
# rather than applying it to the target unconditionally.
if(APPLE)
    set_target_properties(reversi-app PROPERTIES OUTPUT_NAME "Reversi")
endif()

# M10 phase 5: packaging. Every dev-machine workaround this project has needed so far (prepending
# Qt's bin/ to PATH before running ctest/launching reversi-app.exe directly, documented in the
# root CLAUDE.md) exists because nothing has ever actually deployed Qt's own DLLs/plugins
# alongside the built binary - a real end user shouldn't need to do either of those things.
# qt_generate_deploy_app_script() is Qt 6.3+'s own CMake-native answer to this: one call that
# generates an install-time script invoking windeployqt on Windows and macdeployqt on macOS
# automatically (whichever this configure is targeting) - one code path for both platforms
# instead of two hand-rolled ones, and Qt's own recommended replacement for exactly this class of
# manual-PATH workaround. `cmake --install <build-dir> --config Release --prefix <deploy-dir>`
# then produces a fully self-contained folder: the exe/bundle, every Qt DLL/framework and platform
# plugin (platforms, multimedia) it actually needs, and the sound asset below - this is what both
# the Windows installer/portable-zip and the macOS DMG (packaging/, CI) get built from.
#
# RUNTIME DESTINATION bin, not `.` - confirmed by actually running the generated deploy script and
# launching the result (no Qt on PATH): windeployqt's GENERATE_QT_CONF writes a qt.conf with
# `Prefix = ..` next to the DLLs it deploys, i.e. it assumes the exe and the Qt DLLs sit together
# in their own subdirectory with plugins/ one level up - installing the exe directly into the
# prefix root left it looking for plugins/ in the wrong place entirely. macOS's BUNDLE DESTINATION
# stays `.` - a .app bundle is already fully self-contained internally (Contents/MacOS,
# Contents/Frameworks, ...), it doesn't use this bin/-plus-plugins/ layout at all.
if(APPLE)
    install(TARGETS reversi-app BUNDLE DESTINATION .)
    # applicationDirPath() resolves to Contents/MacOS inside the bundle for a macOS app bundle,
    # not the top-level .app folder - TARGET_BUNDLE_CONTENT_DIR is what actually reaches there.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/move-self.wav"
            DESTINATION "$<TARGET_BUNDLE_CONTENT_DIR:reversi-app>/MacOS")
else()
    install(TARGETS reversi-app RUNTIME DESTINATION bin)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/move-self.wav" DESTINATION bin)
    # Shipped alongside the exe purely so packaging/windows/reversi.iss.in can point its own
    # SetupIconFile at it via the same {#MyDeployDir}\bin path every other packaged file uses -
    # the exe's own taskbar/Explorer icon already comes from win_resources.rc above, independent
    # of this copy.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/reversi.ico" DESTINATION bin)
endif()

qt_generate_deploy_app_script(
    TARGET reversi-app
    OUTPUT_SCRIPT deploy_script
    NO_TRANSLATIONS
)
install(SCRIPT ${deploy_script})

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
