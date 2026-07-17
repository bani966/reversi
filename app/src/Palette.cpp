#include "Palette.hpp"

namespace chrome {

namespace {
const Palette& darkPalette() {
    // M10: a navy-slate retune was tried and reverted - the reference screenshot that prompted it
    // turned out to be a non-default chess.com theme, and the neutral near-black chrome below
    // reads better in practice (confirmed against chess.com's own actual default dark theme,
    // which is neutral near-black too, not blue).
    static const Palette kPalette{
        .windowBackground = QColor(24, 24, 26),
        .popupBackground = QColor(30, 30, 32),
        .panelHover = QColor(42, 42, 46),
        .panelBorder = QColor(51, 51, 54),
        .textColor = QColor(240, 230, 210, 250),
        // chess.com's convention: a soft tint under the square, not a solid fill, so it sits
        // beneath the disc without competing with it for attention.
        .lastMoveHighlightColor = QColor(220, 215, 195, 46),
        // Solid, muted gold - reads as emphasis against the dark chrome without the saturation of
        // a literal "gold" hue, which would clash with the app's otherwise desaturated palette.
        .accentColor = QColor(198, 158, 88),
        .accentTextColor = QColor(26, 24, 20),
        .secondaryTextColor = QColor(150, 148, 140),
    };
    return kPalette;
}

// M10 phase 3: mirrors darkPalette()'s role hierarchy exactly (popupBackground lifted off
// windowBackground, panelHover a further step, panelBorder visible-but-subtle) just inverted in
// lightness - warm off-white/near-white backgrounds, warm near-black text, matching darkPalette()'s
// own warm cream-on-near-black pairing rather than a cold gray inversion. accentColor and
// lastMoveHighlightColor are intentionally identical to darkPalette()'s - see the Theme enum's own
// doc comment in Palette.hpp for why neither needed a light-specific value.
const Palette& lightPalette() {
    static const Palette kPalette{
        .windowBackground = QColor(245, 243, 238),
        .popupBackground = QColor(255, 255, 255),
        .panelHover = QColor(228, 225, 218),
        .panelBorder = QColor(210, 207, 198),
        .textColor = QColor(30, 28, 24, 250),
        .lastMoveHighlightColor = QColor(220, 215, 195, 46),
        .accentColor = QColor(198, 158, 88),
        .accentTextColor = QColor(26, 24, 20),
        .secondaryTextColor = QColor(110, 108, 100),
    };
    return kPalette;
}
} // namespace

ThemeManager& ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

void ThemeManager::setTheme(Theme theme) {
    if (theme == theme_) {
        return; // no-op: nothing actually changed, so no reason to make every subscriber re-style
    }
    theme_ = theme;
    emit themeChanged(theme_);
}

const Palette& palette() {
    return ThemeManager::instance().currentTheme() == Theme::Dark ? darkPalette() : lightPalette();
}

QString panelControlsStyleSheet() {
    const Palette& theme = palette();
    return QStringLiteral(R"(
        QFrame#sidePanel {
            background-color: %5;
            border-radius: 6px;
        }
        QPushButton {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 4px;
            padding: 8px;
            font-family: "Segoe UI";
            font-weight: 600;
        }
        QPushButton:hover:enabled {
            background-color: %4;
        }
        QPushButton:disabled {
            color: %3;
        }
        QLabel {
            color: %2;
            font-family: "Segoe UI";
        }
        QGroupBox {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 6px;
            margin-top: 20px;
            font-family: "Segoe UI";
            font-weight: 600;
            padding-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 8px;
            padding: 2px 4px;
            color: %2;
            background-color: transparent;
        }
        QCheckBox {
            color: %2;
            font-family: "Segoe UI";
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 14px;
            height: 14px;
            border: 1px solid %3;
            border-radius: 3px;
            background-color: %5;
        }
        QCheckBox::indicator:checked {
            background-color: %6;
            border: 1px solid %6;
        }
        QCheckBox::indicator:disabled {
            border: 1px solid %3;
            background-color: %1;
        }
        QSpinBox {
            background-color: %5;
            color: %2;
            border: 1px solid %3;
            border-radius: 4px;
            padding: 4px;
            font-family: "Segoe UI";
        }
    )")
        .arg(theme.popupBackground.name())  // %1
        .arg(theme.textColor.name())        // %2
        .arg(theme.panelBorder.name())      // %3
        .arg(theme.panelHover.name())       // %4
        .arg(theme.windowBackground.name()) // %5 - the panel's own base shade (QFrame#sidePanel),
                                            // one level darker than the popupBackground (%1)
                                            // cards/controls sitting on top of it - also reused as
                                            // the recessed control fill for QCheckBox::indicator/
                                            // QSpinBox, one shade darker than their surrounding
                                            // card
        .arg(theme.accentColor.name());     // %6 - checked checkbox fill, the same solid
                                            // emphasis color used elsewhere (MultiPV top-line
                                            // badge, move-history current-row indicator)
}

} // namespace chrome
