#include "TitleBarWidget.hpp"

#include "Palette.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QWindow>

namespace {
constexpr int kTitleBarHeight = 36;
constexpr int kButtonWidth = 46;

// Stage 1 is functional correctness, not final styling (that's a later stage) - this is
// deliberately minimal so the buttons read as intentional rather than default-Qt-white against
// the dark bar, without trying to nail hover/pressed polish yet.
QPushButton* makeButton(const QString& glyph, QWidget* parent) {
    auto* button = new QPushButton(glyph, parent);
    button->setFixedSize(kButtonWidth, kTitleBarHeight);
    button->setFlat(true);
    button->setFocusPolicy(Qt::NoFocus);
    const chrome::Palette& theme = chrome::palette();
    button->setStyleSheet(
        QStringLiteral("QPushButton { background: transparent; color: %1; "
                       "border: none; font-family: 'Segoe UI'; font-size: 11px; } "
                       "QPushButton:hover { background-color: %2; }")
            .arg(theme.textColor.name(), theme.panelHover.name()));
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

    minimizeButton_ = makeButton(QStringLiteral("−"), this); // minus sign
    maximizeButton_ = makeButton(QStringLiteral("□"), this); // white square
    closeButton_ = makeButton(QStringLiteral("✕"), this);    // multiplication x

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
