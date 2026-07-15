#pragma once

#include <QMainWindow>

class BoardWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    BoardWidget* board_;
};
