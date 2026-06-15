#include "gui/MainWindow.hpp"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Kubik"));
    app.setOrganizationName(QStringLiteral("Kubik"));

    // Нативный Windows-стиль игнорирует QPalette (меню, спинбоксы остаются светлыми).
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    kubik::MainWindow window;
    window.show();

    return app.exec();
}
