#include "Palette.hpp"

namespace chrome {

const Palette& palette() {
    static const Palette kPalette{
        .windowBackground = QColor(24, 24, 26),
        .popupBackground = QColor(30, 30, 32),
        .panelHover = QColor(42, 42, 46),
        .panelBorder = QColor(51, 51, 54),
        .textColor = QColor(240, 230, 210, 250),
    };
    return kPalette;
}

} // namespace chrome
