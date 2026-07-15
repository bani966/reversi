#pragma once

#include <QMainWindow>

class BoardWidget;
class GameController;
class QCloseEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    BoardWidget* board_;
    GameController* controller_;

    void createGameMenu();
};
