#pragma once

#include <QDialog>

#include <vector>

#include "kubik/FftFilter.hpp"
#include "SliceView.hpp"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QLabel;

namespace kubik {

class Spectrum2DPlotWidget;
class SlicePreviewWidget;

/// 2D FFT footprint-фильтр для time-срезов.
class FftFilter2DDialog : public QDialog {
    Q_OBJECT
public:
    FftFilter2DDialog(const std::vector<float>& slice, int w, int h, int h0, int h1, int v0, int v1,
                      double d_xl, double d_il, ColorMap color_map, float clip_percent = 99.f,
                      QWidget* parent = nullptr);

signals:
    void applyToCubeRequested(const FftFilter2DParams& params);

private slots:
    void onFilterTypeChanged(int index);
    void onParamsChanged();
    void onPlotFilterParamsChanged(const FftFilter2DParams& params);
    void onSpectrumClipChanged(int value);
    void onPreviewClipChanged(int value);
    void onApply();
    void onUndo();
    void onApplyToCube();

private:
    void setupUi();
    void updateSpinVisibility();
    void updateSpectrumClipLabel();
    void updatePreviewClipLabel();
    void applyPreviewDisplay();
    void refreshPreviews();
    void updatePreviews(const std::vector<float>& before_region, const std::vector<float>& after_region);
    Spectrum2D spectrumFromRegion(const std::vector<float>& data) const;
    std::vector<float> extractRegion(const std::vector<float>& data) const;
    FftFilter2DParams currentParams() const;

    std::vector<float> slice_;
    std::vector<float> filtered_slice_;
    std::vector<float> preview_before_data_;
    std::vector<float> preview_after_data_;
    std::vector<float> preview_diff_data_;
    Spectrum2D original_spec_;
    int w_ = 0;
    int h_ = 0;
    int h0_ = 0;
    int h1_ = 0;
    int v0_ = 0;
    int v1_ = 0;
    double d_xl_ = 1.0;
    double d_il_ = 1.0;
    ColorMap color_map_ = ColorMap::Grayscale;
    float spectrum_clip_percent_ = 99.f;
    float preview_clip_percent_ = 99.f;
    float preview_vmin_ = 0.f;
    float preview_vmax_ = 1.f;

    Spectrum2DPlotWidget* spectrum_plot_ = nullptr;
    SlicePreviewWidget* preview_before_ = nullptr;
    SlicePreviewWidget* preview_after_ = nullptr;
    SlicePreviewWidget* preview_diff_ = nullptr;
    QComboBox* type_combo_ = nullptr;
    QDoubleSpinBox* k_cut_il_spin_ = nullptr;
    QDoubleSpinBox* k_cut_xl_spin_ = nullptr;
    QDoubleSpinBox* k_pass_spin_ = nullptr;
    QDoubleSpinBox* k_smooth_spin_ = nullptr;
    QSpinBox* spectrum_clip_spin_ = nullptr;
    QSpinBox* preview_clip_spin_ = nullptr;
    QLabel* spectrum_clip_range_label_ = nullptr;
    QLabel* preview_clip_range_label_ = nullptr;
    QPushButton* apply_btn_ = nullptr;
    QPushButton* undo_btn_ = nullptr;
    QPushButton* apply_to_cube_btn_ = nullptr;
};

}  // namespace kubik
