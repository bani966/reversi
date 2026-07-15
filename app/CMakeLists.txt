# Qt GUI. Qt types must never leak into engine/.
# Multimedia is linked from M0 so the toolchain (and the aqt qtmultimedia module
# install) is verified long before QSoundEffect is actually used.
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)
qt_standard_project_setup()

# GameController runs AI search on a std::thread; MSVC needs nothing extra, but Threads::Threads
# is the portable way to get pthread linked in on Linux/macOS toolchains too.
find_package(Threads REQUIRED)

qt_add_executable(reversi-app
    src/main.cpp
    src/MainWindow.cpp
    src/BoardWidget.cpp
    src/GameController.cpp
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
