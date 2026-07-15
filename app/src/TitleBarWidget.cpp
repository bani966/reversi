#include "TitleBarWidget.hpp"

#include "Palette.hpp"

#include <QColor>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QWindow>

namespace {
constexpr int kTitleBarHeight = 36;
constexpr int kButtonWidth = 46;

// Windows 11's own close-button hover red - matching the platform convention rather than
// reusing the generic panel-hover gray, since "close" is the one button worth visually
// distinguishing before you click it.
const QColor kCloseHoverColor(196, 43, 28);

struct IconSet {
    QString fontFamily; // empty means "no icon font available, use plain Unicode glyphs"
    QString minimize;
    QString maximize;
    QString restore;
    QString close;
};

// Windows' own caption-button glyphs, via the same icon font Windows itself uses to draw its
// real minimize/maximize/close buttons (Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on
// Windows 10 - both use these codepoints). Checked at runtime, not assumed: neither font
// exists on macOS/Linux or pre-2020 Windows, where these codepoints would render as empty
// boxes - falls back to plain Unicode glyphs in the regular UI font there instead.
const IconSet& iconSet() {
    static const IconSet kIconSet = [] {
        const QStringList available = QFontDatabase::families();
        for (const QString& family :
             {QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")}) {
            if (available.contains(family)) {
                return IconSet{family, QString(QChar(0xE921)), QString(QChar(0xE922)),
                               QString(QChar(0xE923)), QString(QChar(0xE8BB))};
            }
        }
        return IconSet{QString(), QStringLiteral("−"), QStringLiteral("□"), QStringLiteral("▣"),
                       QStringLiteral("✕")};
    }();
    return kIconSet;
}

QPushButton* makeButton(const QString& glyph, QWidget* parent, const QColor& hoverColor) {
    auto* button = new QPushButton(glyph, parent);
    button->setFixedSize(kButtonWidth, kTitleBarHeight);
    button->setFlat(true);
    button->setFocusPolicy(Qt::NoFocus);
    const chrome::Palette& theme = chrome::palette();
    const QString family =
        iconSet().fontFamily.isEmpty() ? QStringLiteral("Segoe UI") : iconSet().fontFamily;
    button->setStyleSheet(QStringLiteral("QPushButton { background: transparent; color: %1; "
                                         "border: none; font-family: '%2'; font-size: 10px; } "
                                         "QPushButton:hover { background-color: %3; }")
                              .arg(theme.textColor.name(), family, hoverColor.name()));
    return button;
}
} // namespace

TitleBarWidget::TitleBarWidget(QWidget* parent) : QWidget(parent) {
    const chrome::Palette& theme = chrome::palette();
    setFixedHeight(kTitleBarHeight);
    setStyleSheet(QStringLiteral("TitleBarWidget { background-color: %1; }")
                      .arg(theme.windowBackground.name()));

    titleLabel_ = new QLabel(this);
    titleLabel_->setStyleSheet(
        QStringLiteral("color: %1; font-family: 'Segoe UI'; font-weight: 500; padding-left: 10px;")
            .arg(theme.textColor.name()));

    minimizeButton_ = makeButton(iconSet().minimize, this, theme.panelHover);
    maximizeButton_ = makeButton(iconSet().maximize, this, theme.panelHover);
    closeButton_ = makeButton(iconSet().close, this, kCloseHoverColor);

    connect(minimizeButton_, &QPushButton::clicked, this, &TitleBarWidget::minimizeRequested);
    connect(maximizeButton_, &QPushButton::clicked, this,
            &TitleBarWidget::maximizeRestoreRequested);
    connect(closeButton_, &QPushButton::clicked, this, &TitleBarWidget::closeRequested);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleLabel_);
    layout->addStretch(1);
    layout->addWidget(minimizeButton_);
    layout->addWidget(maximizeButton_);
    layout->addWidget(closeButton_);
}

void TitleBarWidget::setTitle(const QString& title) {
    titleLabel_->setText(title);
}

void TitleBarWidget::setMaximized(bool maximized) {
    maximizeButton_->setText(maximized ? iconSet().restore : iconSet().maximize);
    maximizeButton_->setToolTip(maximized ? QStringLiteral("Restore") : QStringLiteral("Maximize"));
}

void TitleBarWidget::mousePressEvent(QMouseEvent* event) {
    // Clicks on the three buttons are delivered to them directly (they're child widgets), so
    // this only ever fires for the "empty" title bar area - no manual hit-testing needed.
    if (event->button() == Qt::LeftButton) {
        if (QWindow* handle = window()->windowHandle()) {
            handle->startSystemMove();
        }
    }
    QWidget::mousePressEvent(event);
}

void TitleBarWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit maximizeRestoreRequested();
    }
    QWidget::mouseDoubleClickEvent(event);
}
