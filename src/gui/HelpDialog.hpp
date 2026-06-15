#pragma once

#include <QString>

class QWidget;

namespace kubik {

class HelpDialog {
public:
    static void show(QWidget* parent, const QString& title, const QString& html);
};

}  // namespace kubik
