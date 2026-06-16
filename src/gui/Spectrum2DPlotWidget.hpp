#pragma once

#include <QWidget>

#include <vector>

#include "kubik/FftFilter.hpp"

namespace kubik {

/// 2D спектр k_XL × k_IL (тепловая карта) с опциональным overlay маски.
class Spectrum2DPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit Spectrum2DPlotWidget(QWidget* parent = nullptr);

    void setSpectrum(const Spectrum2D& spec);
    void setMaskOverlay(const std::vector<float>& mask, int w, int h);
    void setMaskOverlayEnabled(bool enabled);
    void setClipPercent(float clip_percent);
    float clipPercent() const { return clip_percent_; }
    double ampClipMin() const { return amp_vmin_; }
    double ampClipMax() const { return amp_vmax_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRectF plotRect() const;
    void updateAmpClipRange();

    Spectrum2D spec_;
    std::vector<float> mask_;
    int mask_w_ = 0;
    int mask_h_ = 0;
    bool mask_enabled_ = false;
    float clip_percent_ = 99.f;
    double amp_vmin_ = 0.0;
    double amp_vmax_ = 1.0;
};

}  // namespace kubik
