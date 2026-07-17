#pragma once

#include <QColor>
#include <QObject>
#include <QString>

// Shared chrome color roles for app/ (Qt widgets only - engine/ has nothing to do with this).
// BoardWidget's own board-specific colors (felt green, disc fills, grid lines) stay local to
// BoardWidget.cpp and constant across themes (see that file's own doc comment on BoardPalette for
// why) - only values genuinely shared across multiple widgets - window/chrome background, the
// text color used for both coordinate labels and menu/status/title bar text - live here, so they
// can only go out of sync if this file itself is edited inconsistently, not by drifting copies
// scattered across files.
namespace chrome {

struct Palette {
    QColor windowBackground; // title bar, menu bar, status bar
    QColor popupBackground;  // menu dropdowns - slightly lifted off windowBackground
    QColor panelHover;       // hover/selected/pressed state on menu items and title bar buttons
    QColor panelBorder;      // thin borders/separators
    QColor textColor;        // shared by coordinate labels and all chrome text
    QColor lastMoveHighlightColor; // low-opacity amber tint under the most recently played disc
    // M9 phase 5: a solid, muted gold - deliberately a SEPARATE role from lastMoveHighlightColor
    // above, not a reuse of it. lastMoveHighlightColor is a specific low-alpha board-overlay tint
    // (chess.com's "soft tint under the square" convention, sized only for that use); accentColor
    // is a solid emphasis color used sparingly across chrome (the MultiPV panel's top-ranked-line
    // badge, the move-history list's current-row indicator) - overloading the board-overlay
    // meaning onto general-purpose emphasis would make either use harder to change independently
    // later.
    QColor accentColor;
    // M10 phase 3: text drawn directly on top of a solid accentColor fill (the toolbar "Analysis"
    // button's :checked state, the MultiPV top-line rank badge) - a fixed dark color, deliberately
    // NOT theme.windowBackground (what both of those used before this role existed):
    // windowBackground is dark in the dark theme and light in the light theme, but accentColor's
    // own gold stays the SAME medium-light tone in both - reusing windowBackground as a "this reads
    // as dark text" proxy silently broke the moment a light theme existed (near-white text on
    // gold). Identical to accentColor itself, this role doesn't invert with theme.
    QColor accentTextColor;
    // M10 phase 3: de-emphasized text (the MultiPV card's depth/nodes meta line and PV line) -
    // dimmer than textColor but still legible. A REAL bug this role fixes: the first light-theme
    // pass reused `theme.panelHover.lighter(160)` for this (a dark-theme-only trick - lightening an
    // already-dark panelHover produces a visible dim gray on a dark card), which silently broke in
    // the light theme instead: lightening an ALREADY-LIGHT panelHover pushed it toward near-white,
    // reading as washed-out/invisible text on the light theme's equally-light cards. A named role
    // with its own correct value per theme (dimmer than textColor in BOTH directions, not derived
    // via a lightness transform that only makes sense in one) is what actually fixes this, not a
    // different multiplier.
    QColor secondaryTextColor;
};

// M10 phase 3: two named themes. accentColor/lastMoveHighlightColor are deliberately identical in
// both palettes - neither depends on overall chrome lightness (the gold accent and the board-
// overlay tint both read fine against either background), so there was nothing real to invert
// there; only background/text roles actually differ between the two.
enum class Theme { Dark, Light };

// Meyers-singleton QObject so every theme-dependent widget can independently subscribe to
// themeChanged rather than MainWindow having to reach into each one's internals - each widget
// that builds a chrome::palette()-derived stylesheet/QPalette owns re-applying it on this signal
// (see BoardWidget/TitleBarWidget/MainWindow/SettingsDialog's own themeChanged connections).
class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    Theme currentTheme() const { return theme_; }
    void setTheme(Theme theme);

signals:
    void themeChanged(Theme newTheme);

private:
    ThemeManager() = default;
    Theme theme_ = Theme::Dark;
};

// Returns darkPalette() or lightPalette() depending on ThemeManager::instance().currentTheme() -
// every existing call site already calls this fresh (never caches the returned reference across a
// theme change), so making it theme-aware needed no changes anywhere else in app/.
const Palette& palette();

// M9 phase 5: shared QSS for every non-board panel control (QPushButton/QLabel/QGroupBox/
// QCheckBox/QSpinBox) built from palette() above - one function so panel_'s analysis/move-
// history panes and SettingsDialog (previously entirely unstyled) can't drift out of sync with
// each other the way two independently-hand-copied stylesheets could. Controls (QPushButton,
// QSpinBox) use a 4px radius; containers (QGroupBox, and panel_ itself via its own
// QFrame#sidePanel selector, set by whichever caller applies this stylesheet) use 6px - the same
// "cards vs. controls" radius hierarchy the MultiPV row cards and panel_'s own move-history/
// analysis panes (MainWindow.cpp) already use, and matching BoardWidget.cpp's own board-corner
// radius (kept equal by hand across the two files - see BoardWidget.cpp's kCornerRadius comment).
// M10: QFrame#sidePanel no longer has a stroked border - chess.com's own panel is background-color
// only, with lighter cards (popupBackground) sitting on top of it for section separation, not a
// border line.
// M10 phase 3: QGroupBox's margin-top was 12px, too tight for its own bold title text's actual
// line height - the title was getting clipped by the box's own top border. Invisible-by-accident
// in the dark theme (clipped dark-on-dark text barely registers), but unmistakable once the light
// theme's contrast made the cut-off obvious - bumped to 20px, a real layout fix, not a color one.
// A second, separate QGroupBox bug found in the same area: the title sits in the "margin" strip
// above the box's own painted background (subcontrol-origin: margin), outside the region QSS's
// `QGroupBox { background-color }` actually fills - `color: %2` on `QGroupBox::title` alone wasn't
// reliably taking effect there under Fusion, so the title fell back to a default/inherited text
// color instead. Fixed by also setting `color` on the outer `QGroupBox` selector itself (not just
// `::title`) and giving `::title` an explicit `background-color: transparent` - both together are
// what actually makes the title's own color win instead of falling back.
QString panelControlsStyleSheet();

} // namespace chrome
