#pragma once

#include <QColor>

// Shared chrome color roles for app/ (Qt widgets only - engine/ has nothing to do with this).
// BoardWidget's own board-specific colors (felt green, disc fills, grid lines) stay local to
// BoardWidget.cpp; only values genuinely shared across multiple widgets - window/chrome
// background, the cream text color used for both coordinate labels and menu/status/title bar
// text - live here, so they can only go out of sync if this file itself is edited
// inconsistently, not by drifting copies scattered across files.
namespace chrome {

struct Palette {
    QColor windowBackground; // title bar, menu bar, status bar
    QColor popupBackground;  // menu dropdowns - slightly lifted off windowBackground
    QColor panelHover;       // hover/selected/pressed state on menu items and title bar buttons
    QColor panelBorder;      // thin borders/separators
    QColor textColor;        // warm cream, shared by coordinate labels and all chrome text
};

const Palette& palette();

} // namespace chrome
