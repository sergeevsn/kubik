#pragma once

#include <QWidget>

#include <vector>

#include "kubik/FftFilter.hpp"

namespace kubik {

/// 2D спектр k_XL × k_IL (тепловая карта) с overlay footprint-полос.
class Spectrum2DPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit Spectrum2DPlotWidget(QWidget* parent = nullptr);

    void setSpectrum(const Spectrum2D& spec);
    void setFilterParams(const FftFilter2DParams& params);
    void setClipPercent(float clip_percent);
    float clipPercent() const { return clip_percent_; }
    double ampClipMin() const { return amp_vmin_; }
    double ampClipMax() const { return amp_vmax_; }

signals:
    void filterParamsChanged(const FftFilter2DParams& params);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class DragTarget { None, PassIl, CutIl, PassXl, CutXl };

    QRectF plotRect() const;
    double kXlAtX(int px) const;
    double kIlAtY(int py) const;
    int xAtKXl(double k) const;
    int yAtKIl(double k) const;
    double kMaxXl() const;
    double kMaxIl() const;
    DragTarget hitTest(int px, int py) const;
    void applyDrag(int px, int py);
    void updateHoverCursor(int px, int py);
    void updateAmpClipRange();

    Spectrum2D spec_;
    FftFilter2DParams params_;
    float clip_percent_ = 99.f;
    double amp_vmin_ = 0.0;
    double amp_vmax_ = 1.0;
    DragTarget drag_ = DragTarget::None;
};

}  // namespace kubik
