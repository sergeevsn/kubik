#pragma once

#include <QWidget>

class QToolButton;

namespace kubik {

/// Сворачиваемая секция сайдбара с заголовком-кнопкой.
class CollapsibleGroupBox : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleGroupBox(const QString& title, QWidget* parent = nullptr);

    QWidget* contentWidget() const;
    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded_; }

private:
    void updateArrow();

    QToolButton* toggle_ = nullptr;
    QWidget* content_ = nullptr;
    bool expanded_ = true;
};

}  // namespace kubik
