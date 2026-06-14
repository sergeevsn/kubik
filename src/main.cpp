#include "gui/MainWindow.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Kubik"));
    app.setOrganizationName(QStringLiteral("Kubik"));

    kubik::MainWindow window;
    window.show();

    return app.exec();
}
