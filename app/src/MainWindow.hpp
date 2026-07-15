#pragma once

#include <QMainWindow>

class BoardWidget;
class GameController;
class TitleBarWidget;
class QCloseEvent;
class QMenuBar;
class QStatusBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    TitleBarWidget* titleBar_;
    QMenuBar* menuBar_;
    BoardWidget* board_;
    QStatusBar* statusBar_;
    GameController* controller_;

    void createGameMenu();
};
