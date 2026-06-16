#pragma once

#include <QDialog>

#include <vector>

#include "kubik/FftFilter.hpp"
#include "kubik/SegyCube.hpp"
#include "SliceView.hpp"

class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QLabel;
class QCheckBox;

namespace kubik {

class Spectrum2DPlotWidget;
class SlicePreviewWidget;
class SegyCube;

/// 2D FFT footprint-фильтр для time-срезов.
class FftFilter2DDialog : public QDialog {
    Q_OBJECT
public:
    FftFilter2DDialog(const SegyCube* cube, int current_t_idx, CropBounds crop, ResampleParams resample,
                      const std::vector<float>& slice, int w, int h, double d_xl, double d_il,
                      ColorMap color_map, float clip_percent = 99.f, QWidget* parent = nullptr);

signals:
    void applyToCubeRequested(const FftFilter2DParams& params);

private slots:
    void onParamsChanged();
    void onSpectrumClipChanged(int value);
    void onPreviewClipChanged(int value);
    void onUndo();
    void onApplyToCube();
    void onMaskOverlayToggled(bool enabled);

private:
    void setupUi();
    void updateSpectrumClipLabel();
    void updatePreviewClipLabel();
    void applyPreviewDisplay();
    void refreshPreviews();
    void updatePreviews(const std::vector<float>& before_slice, const std::vector<float>& after_slice);
    void recompute();
    Spectrum2D averageSpectrum(int avg_count) const;
    FftFilter2DParams currentParams() const;

    const SegyCube* cube_ = nullptr;
    int current_t_idx_ = 0;
    CropBounds crop_{};
    ResampleParams resample_{};
    std::vector<float> slice_;
    std::vector<float> filtered_slice_;
    std::vector<float> preview_before_data_;
    std::vector<float> preview_after_data_;
    std::vector<float> preview_diff_data_;
    Spectrum2D original_spec_;
    int w_ = 0;
    int h_ = 0;
    double d_xl_ = 1.0;
    double d_il_ = 1.0;
    ColorMap color_map_ = ColorMap::Grayscale;
    float spectrum_clip_percent_ = 99.f;
    float preview_clip_percent_ = 99.f;
    float preview_vmin_ = 0.f;
    float preview_vmax_ = 1.f;

    Spectrum2DPlotWidget* spectrum_before_plot_ = nullptr;
    Spectrum2DPlotWidget* spectrum_after_plot_ = nullptr;
    SlicePreviewWidget* preview_before_ = nullptr;
    SlicePreviewWidget* preview_after_ = nullptr;
    SlicePreviewWidget* preview_diff_ = nullptr;
    QDoubleSpinBox* notch_width_spin_ = nullptr;
    QDoubleSpinBox* suppression_spin_ = nullptr;
    QDoubleSpinBox* sensitivity_spin_ = nullptr;
    QDoubleSpinBox* k_preserve_spin_ = nullptr;
    QSpinBox* avg_count_spin_ = nullptr;
    QCheckBox* mask_overlay_check_ = nullptr;
    QSpinBox* spectrum_clip_spin_ = nullptr;
    QSpinBox* preview_clip_spin_ = nullptr;
    QLabel* spectrum_clip_range_label_ = nullptr;
    QLabel* preview_clip_range_label_ = nullptr;
    QPushButton* undo_btn_ = nullptr;
    QPushButton* apply_to_cube_btn_ = nullptr;
};

}  // namespace kubik
