#include "Palette.hpp"

namespace chrome {

const Palette& palette() {
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
    };
    return kPalette;
}

} // namespace chrome
