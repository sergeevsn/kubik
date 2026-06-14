#pragma once

#include <QWidget>

#include "kubik/types.hpp"

namespace kubik {

/// Мини-навигатор куба: плоскость текущего среза в выбранной проекции.
class CubeNavigator : public QWidget {
    Q_OBJECT
public:
    explicit CubeNavigator(QWidget* parent = nullptr);

    void setCubeSize(int n_il, int n_xl, int n_t);
    void setSlicePositions(int il_idx, int xl_idx, int t_idx);
    void setSliceMode(SliceMode mode);

signals:
    void inlineJumpRequested(int il_idx);
    void crosslineJumpRequested(int xl_idx);
    void timeJumpRequested(int t_idx);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRectF cubeRect() const;
    void mapClick(const QPoint& pos);

    int n_il_ = 1;
    int n_xl_ = 1;
    int n_t_ = 1;
    int il_idx_ = 0;
    int xl_idx_ = 0;
    int t_idx_ = 0;
    SliceMode mode_ = SliceMode::Inline;
};

}  // namespace kubik
