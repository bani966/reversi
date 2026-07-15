#include "MainWindow.hpp"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // Windows' default style (windowsvista/windows11) delegates part of its chrome painting
    // (QMenuBar, QStatusBar, ...) to native Win32 theme APIs, which ignore stylesheet color
    // properties. Fusion is pure-Qt-painted with no native delegation, so MainWindow's QSS
    // pass is actually authoritative instead of only partially applying.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    MainWindow window;
    window.show();

    return app.exec();
}
