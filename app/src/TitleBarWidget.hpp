#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

// Custom title bar for the frameless MainWindow: drag-to-move and double-click-to-maximize
// (via QWindow::startSystemMove(), not manual delta-tracking - this keeps OS gestures like
// drag-to-top-edge-to-maximize working for free), plus hand-styled minimize/maximize/close
// buttons standing in for the native ones a frameless window no longer has. Emits requests
// rather than acting directly: MainWindow owns the actual window-state transitions.
class TitleBarWidget : public QWidget {
    Q_OBJECT

public:
    explicit TitleBarWidget(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    // Reflects the window's current maximized state in the maximize button (restore vs.
    // maximize glyph/tooltip) - MainWindow is the source of truth for this, not this widget.
    void setMaximized(bool maximized);

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QLabel* titleLabel_;
    QPushButton* minimizeButton_;
    QPushButton* maximizeButton_;
    QPushButton* closeButton_;

    // M10 phase 3: applies every chrome::palette()-derived style here (background, title text,
    // button hover colors) - called once at the end of construction and again on every
    // chrome::ThemeManager::themeChanged, so this is the single place "how this widget looks"
    // lives, not duplicated between an initial construction pass and a separate refresh path.
    void refreshTheme();
};
