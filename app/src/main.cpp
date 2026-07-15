#include <QApplication>
#include <QLabel>
#include <QMainWindow>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QMainWindow window;
    auto* label = new QLabel(QStringLiteral("Reversi — the board arrives in M3"));
    label->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(label);
    window.setWindowTitle(QStringLiteral("Reversi"));
    window.resize(720, 760);
    window.show();

    return app.exec();
}
