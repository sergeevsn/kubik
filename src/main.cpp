#include "gui/MainWindow.hpp"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Kubik"));
    app.setOrganizationName(QStringLiteral("Kubik"));

    // Нативный Windows-стиль игнорирует QPalette (меню, спинбоксы остаются светлыми).
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

#ifdef Q_OS_WIN
    // Fusion на Windows: line edit спинбокса перекрывает стрелки (видны, но не кликаются).
    app.setStyleSheet(QStringLiteral(
        "QSpinBox, QDoubleSpinBox, QAbstractSpinBox { padding-right: 16px; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button, QAbstractSpinBox::up-button {"
        "  width: 16px; subcontrol-origin: border; subcontrol-position: top right; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button, QAbstractSpinBox::down-button {"
        "  width: 16px; subcontrol-origin: border; subcontrol-position: bottom right; }"));
#endif

    kubik::MainWindow window;
    window.show();

    return app.exec();
}
