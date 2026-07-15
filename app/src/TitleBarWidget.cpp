#include "TitleBarWidget.hpp"

#include "Palette.hpp"

#include <QColor>
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

QPushButton* makeButton(const QString& glyph, QWidget* parent, const QColor& hoverColor) {
    auto* button = new QPushButton(glyph, parent);
    button->setFixedSize(kButtonWidth, kTitleBarHeight);
    button->setFlat(true);
    button->setFocusPolicy(Qt::NoFocus);
    const chrome::Palette& theme = chrome::palette();
    button->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; color: %1; "
                       "border: none; font-family: 'Segoe UI'; font-size: 11px; } "
                       "QPushButton:hover { background-color: %2; }")
            .arg(theme.textColor.name(), hoverColor.name()));
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

    minimizeButton_ = makeButton(QStringLiteral("−"), this, theme.panelHover); // minus sign
    maximizeButton_ = makeButton(QStringLiteral("□"), this, theme.panelHover); // white square
    closeButton_ = makeButton(QStringLiteral("✕"), this, kCloseHoverColor);    // multiplication x

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
    maximizeButton_->setText(maximized ? QStringLiteral("▣") : QStringLiteral("□"));
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
