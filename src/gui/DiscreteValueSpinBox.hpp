#pragma once

#include <QAbstractSpinBox>
#include <QSize>
#include <QTimer>
#include <QVector>

namespace kubik {

/// Спинбокс по дискретному списку значений (IL, XL, время в мс).
class DiscreteValueSpinBox : public QAbstractSpinBox {
    Q_OBJECT
public:
    explicit DiscreteValueSpinBox(QWidget* parent = nullptr);

    void setValues(const QVector<int>& values);
    int currentIndex() const { return index_; }
    int currentValue() const;
    void setCurrentIndex(int idx);

signals:
    void currentIndexChanged(int idx);

protected:
    void stepBy(int steps) override;
    QValidator::State validate(QString& input, int& pos) const override;
    void fixup(QString& input) const override;
    QSize minimumSizeHint() const override;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    StepEnabled stepEnabled() const override;
#endif

private:
    void commitEditedValue();
    void refreshStepButtons();
    void updateText();
    int indexForValue(int value) const;

    QVector<int> values_;
    int index_ = 0;
    QTimer* debounce_timer_ = nullptr;
};

}  // namespace kubik
