#include "SpinBoxFix.hpp"

#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTimer>

namespace kubik {

namespace {

void initSpinBoxOption(const QAbstractSpinBox* spin, QStyleOptionSpinBox* opt) {
    opt->initFrom(spin);
    opt->rect = spin->rect();
    opt->frame = spin->hasFrame();
    opt->buttonSymbols = spin->buttonSymbols();
    opt->stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
}

QStyle::SubControl hitTestSpinBox(const QAbstractSpinBox* spin, const QPoint& pos) {
    QStyleOptionSpinBox opt;
    initSpinBoxOption(spin, &opt);
    return spin->style()->hitTestComplexControl(QStyle::CC_SpinBox, &opt, pos, spin);
}

QLineEdit* spinLineEdit(const QAbstractSpinBox* spin) {
    return spin ? spin->findChild<QLineEdit*>() : nullptr;
}

class SpinBoxFix : public QObject {
public:
    explicit SpinBoxFix(QAbstractSpinBox* spin) : QObject(spin), spin_(spin), edit_(spinLineEdit(spin)) {
        spin_->installEventFilter(this);
        if (edit_) {
            edit_->installEventFilter(this);
        }
        syncLineEditGeometry();
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Polish:
        case QEvent::LayoutRequest:
            syncLineEditGeometry();
            break;
        case QEvent::Show:
            QTimer::singleShot(0, spin_, [this]() { syncLineEditGeometry(); });
            break;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            if (tryStepFromMouse(watched, static_cast<QMouseEvent*>(event))) {
                return true;
            }
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    bool tryStepFromMouse(QObject* watched, const QMouseEvent* mouse) {
        if (!spin_ || !mouse || mouse->button() != Qt::LeftButton) {
            return false;
        }
        QPoint pos;
        if (watched == spin_) {
            pos = mouse->pos();
        } else if (edit_ && watched == edit_) {
            pos = edit_->mapTo(spin_, mouse->pos());
        } else {
            return false;
        }

        const QStyle::SubControl sub = hitTestSpinBox(spin_, pos);
        if (sub == QStyle::SC_SpinBoxUp) {
            spin_->stepUp();
            return true;
        }
        if (sub == QStyle::SC_SpinBoxDown) {
            spin_->stepDown();
            return true;
        }
        return false;
    }

    void syncLineEditGeometry() {
        if (!spin_ || !edit_) {
            edit_ = spinLineEdit(spin_);
        }
        if (!spin_ || !edit_) {
            return;
        }
        QStyleOptionSpinBox opt;
        initSpinBoxOption(spin_, &opt);
        const QRect edit_rect =
            spin_->style()->subControlRect(QStyle::CC_SpinBox, &opt, QStyle::SC_SpinBoxEditField, spin_);
        if (edit_rect.isValid()) {
            edit_->setGeometry(edit_rect);
        }
    }

    QAbstractSpinBox* spin_ = nullptr;
    QLineEdit* edit_ = nullptr;
};

}  // namespace

void setupSpinBox(QAbstractSpinBox* spin) {
    if (!spin) {
        return;
    }
    // Do not emit valueChanged on each typed character; apply after editing is committed.
    spin->setKeyboardTracking(false);
    if (spin->findChild<QObject*>(QStringLiteral("kubik_spinbox_fix"), Qt::FindDirectChildrenOnly)) {
        return;
    }
    auto* fix = new SpinBoxFix(spin);
    fix->setObjectName(QStringLiteral("kubik_spinbox_fix"));

    const QSize hint = spin->minimumSizeHint();
    if (spin->minimumWidth() < hint.width()) {
        spin->setMinimumWidth(hint.width());
    }
}

}  // namespace kubik
