#pragma once

#include <QColor>
#include <QString>

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
    QColor lastMoveHighlightColor; // low-opacity amber tint under the most recently played disc
    // M9 phase 5: a solid, muted gold - deliberately a SEPARATE role from lastMoveHighlightColor
    // above, not a reuse of it. lastMoveHighlightColor is a specific low-alpha board-overlay tint
    // (chess.com's "soft tint under the square" convention, sized only for that use); accentColor
    // is a solid emphasis color used sparingly across chrome (the MultiPV panel's top-ranked-line
    // badge, the move-history list's current-row indicator) - overloading the board-overlay
    // meaning onto general-purpose emphasis would make either use harder to change independently
    // later.
    QColor accentColor;
};

const Palette& palette();

// M9 phase 5: shared QSS for every non-board panel control (QPushButton/QLabel/QGroupBox/
// QCheckBox/QSpinBox) built from palette() above - one function so panel_'s analysis/move-
// history panes and SettingsDialog (previously entirely unstyled) can't drift out of sync with
// each other the way two independently-hand-copied stylesheets could. Controls (QPushButton,
// QSpinBox) use a 4px radius; containers (QGroupBox, and panel_ itself via its own
// QFrame#sidePanel selector, set by whichever caller applies this stylesheet) use 8px - the same
// "cards vs. controls" radius hierarchy the MultiPV row cards (MainWindow.cpp) already use.
QString panelControlsStyleSheet();

} // namespace chrome
