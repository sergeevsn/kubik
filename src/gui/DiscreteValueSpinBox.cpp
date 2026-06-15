#include "DiscreteValueSpinBox.hpp"

#include <QLineEdit>
#include <QShowEvent>
#include <QSignalBlocker>

#include <algorithm>

namespace kubik {

namespace {

constexpr int kSpinBoxButtonColumnWidth = 22;

}  // namespace

void ensureSpinBoxButtonSpace(QAbstractSpinBox* spin) {
    if (!spin) {
        return;
    }
    const QSize hint = spin->minimumSizeHint();
    const int textW = spin->fontMetrics().horizontalAdvance(QStringLiteral("88888888")) + 12;
    const int minW = std::max(hint.width(), textW + kSpinBoxButtonColumnWidth);
    if (spin->minimumWidth() < minW) {
        spin->setMinimumWidth(minW);
    }
    spin->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

namespace {
constexpr int kCommitDebounceMs = 500;
}

DiscreteValueSpinBox::DiscreteValueSpinBox(QWidget* parent) : QAbstractSpinBox(parent) {
    setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    ensureSpinBoxButtonSpace(this);

    debounce_timer_ = new QTimer(this);
    debounce_timer_->setSingleShot(true);
    debounce_timer_->setInterval(kCommitDebounceMs);

    connect(debounce_timer_, &QTimer::timeout, this, [this]() { commitEditedValue(); });
    connect(lineEdit(), &QLineEdit::textChanged, this, [this](const QString&) {
        if (!lineEdit()->hasFocus()) {
            return;
        }
        debounce_timer_->start();
    });
    connect(lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        debounce_timer_->stop();
        commitEditedValue();
    });
}

void DiscreteValueSpinBox::commitEditedValue() {
    if (values_.isEmpty()) {
        return;
    }
    bool ok = false;
    const int v = lineEdit()->text().toInt(&ok);
    if (!ok) {
        updateText();
        return;
    }
    const int idx = indexForValue(v);
    if (idx != index_) {
        index_ = idx;
        updateText();
        refreshStepButtons();
        emit currentIndexChanged(index_);
    } else {
        updateText();
    }
}

void DiscreteValueSpinBox::setValues(const QVector<int>& values) {
    debounce_timer_->stop();
    values_ = values;
    index_ = values_.isEmpty() ? 0 : std::clamp(index_, 0, values_.size() - 1);
    updateText();
    refreshStepButtons();
}

int DiscreteValueSpinBox::currentValue() const {
    if (values_.isEmpty() || index_ < 0 || index_ >= values_.size()) {
        return 0;
    }
    return values_[index_];
}

void DiscreteValueSpinBox::setCurrentIndex(int idx) {
    debounce_timer_->stop();
    if (values_.isEmpty()) {
        index_ = 0;
        updateText();
        return;
    }
    const int clamped = std::clamp(idx, 0, values_.size() - 1);
    if (clamped == index_) {
        updateText();
        return;
    }
    index_ = clamped;
    updateText();
    refreshStepButtons();
}

void DiscreteValueSpinBox::stepBy(int steps) {
    if (values_.isEmpty() || steps == 0) {
        return;
    }
    debounce_timer_->stop();
    const int next = std::clamp(index_ + steps, 0, values_.size() - 1);
    if (next == index_) {
        return;
    }
    index_ = next;
    updateText();
    refreshStepButtons();
    emit currentIndexChanged(index_);
}

QValidator::State DiscreteValueSpinBox::validate(QString& input, int& pos) const {
    Q_UNUSED(pos);
    if (input.isEmpty()) {
        return QValidator::Intermediate;
    }
    bool ok = false;
    input.toInt(&ok);
    return ok ? QValidator::Acceptable : QValidator::Invalid;
}

void DiscreteValueSpinBox::fixup(QString& input) const {
    if (values_.isEmpty() || index_ < 0 || index_ >= values_.size()) {
        return;
    }
    input = QString::number(values_[index_]);
}

QSize DiscreteValueSpinBox::minimumSizeHint() const {
    QSize hint = QAbstractSpinBox::minimumSizeHint();
    const int textW = fontMetrics().horizontalAdvance(QStringLiteral("88888888")) + 12;
    hint.setWidth(std::max(hint.width(), textW + kSpinBoxButtonColumnWidth));
    return hint;
}

void DiscreteValueSpinBox::showEvent(QShowEvent* event) {
    QAbstractSpinBox::showEvent(event);
    ensureSpinBoxButtonSpace(this);
    refreshStepButtons();
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
QAbstractSpinBox::StepEnabled DiscreteValueSpinBox::stepEnabled() const {
    if (values_.isEmpty() || isReadOnly() || !isEnabled()) {
        return StepNone;
    }
    StepEnabled enabled = StepNone;
    if (index_ > 0) {
        enabled |= StepDownEnabled;
    }
    if (index_ < values_.size() - 1) {
        enabled |= StepUpEnabled;
    }
    return enabled;
}
#endif

void DiscreteValueSpinBox::refreshStepButtons() {
    update();
}

void DiscreteValueSpinBox::updateText() {
    QSignalBlocker blocker(lineEdit());
    if (values_.isEmpty()) {
        lineEdit()->clear();
        return;
    }
    index_ = std::clamp(index_, 0, values_.size() - 1);
    lineEdit()->setText(QString::number(values_[index_]));
}

int DiscreteValueSpinBox::indexForValue(int value) const {
    if (values_.isEmpty()) {
        return 0;
    }
    const auto it = std::lower_bound(values_.begin(), values_.end(), value);
    if (it != values_.end() && *it == value) {
        return static_cast<int>(it - values_.begin());
    }
    if (it == values_.begin()) {
        return 0;
    }
    if (it == values_.end()) {
        return values_.size() - 1;
    }
    const int hi = static_cast<int>(it - values_.begin());
    const int lo = hi - 1;
    const int d_lo = value - values_[lo];
    const int d_hi = values_[hi] - value;
    return d_hi < d_lo ? hi : lo;
}

}  // namespace kubik
