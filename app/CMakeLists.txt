# Qt GUI. Qt types must never leak into engine/.
# Multimedia is linked from M0 so the toolchain (and the aqt qtmultimedia module
# install) is verified long before QSoundEffect is actually used.
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)
qt_standard_project_setup()

qt_add_executable(reversi-app
    src/main.cpp
    src/MainWindow.cpp
    src/BoardWidget.cpp
)

target_link_libraries(reversi-app PRIVATE
    reversi::engine
    Qt6::Widgets
    Qt6::Multimedia
    reversi_warnings
)

set_target_properties(reversi-app PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
