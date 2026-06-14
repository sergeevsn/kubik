#include "CollapsibleGroupBox.hpp"

#include <QToolButton>
#include <QVBoxLayout>

namespace kubik {

CollapsibleGroupBox::CollapsibleGroupBox(const QString& title, QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(2);

    toggle_ = new QToolButton(this);
    toggle_->setText(title);
    toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle_->setAutoRaise(true);
    toggle_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toggle_->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; font-weight: bold; text-align: left; padding: 2px 0; }"));

    content_ = new QWidget(this);
    content_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    root->addWidget(toggle_);
    root->addWidget(content_);

    connect(toggle_, &QToolButton::clicked, this, [this]() { setExpanded(!expanded_); });
    setExpanded(true);
}

QWidget* CollapsibleGroupBox::contentWidget() const {
    return content_;
}

void CollapsibleGroupBox::setExpanded(bool expanded) {
    expanded_ = expanded;
    content_->setVisible(expanded_);
    updateArrow();
}

void CollapsibleGroupBox::updateArrow() {
    toggle_->setArrowType(expanded_ ? Qt::DownArrow : Qt::RightArrow);
}

}  // namespace kubik
