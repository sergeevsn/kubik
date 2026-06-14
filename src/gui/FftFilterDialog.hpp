#pragma once

#include <QDialog>

#include <vector>

#include "kubik/FftFilter.hpp"
#include "kubik/types.hpp"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

namespace kubik {

class SpectrumPlotWidget;
class SlicePreviewWidget;

/// Интерактивное окно 1D FFT-фильтрации выделенной области среза.
class FftFilterDialog : public QDialog {
    Q_OBJECT
public:
    FftFilterDialog(const std::vector<float>& slice, int w, int h, int h0, int h1, int v0, int v1,
                    float dt_ms, SliceMode mode, QWidget* parent = nullptr);

signals:
    void applyToCubeRequested(const FftFilterParams& params);

private slots:
    void onFilterTypeChanged(int index);
    void onFrequencySpinChanged();
    void onPlotFrequenciesChanged(double f_low, double f_high);
    void onApply();
    void onUndo();
    void onApplyToCube();

private:
    void setupUi();
    void updateSpinVisibility();
    void updatePreviews(const std::vector<float>& before_region, const std::vector<float>& after_region);
    Spectrum1D spectrumFromRegion(const std::vector<float>& data) const;
    std::vector<float> extractRegion(const std::vector<float>& data) const;
    FftFilterParams currentParams() const;

    std::vector<float> slice_;
    std::vector<float> filtered_slice_;
    Spectrum1D original_spec_;
    int w_ = 0;
    int h_ = 0;
    int h0_ = 0;
    int h1_ = 0;
    int v0_ = 0;
    int v1_ = 0;
    float dt_ms_ = 1.f;
    SliceMode mode_ = SliceMode::Inline;
    float preview_vmin_ = 0.f;
    float preview_vmax_ = 1.f;

    SpectrumPlotWidget* spectrum_plot_ = nullptr;
    SlicePreviewWidget* preview_before_ = nullptr;
    SlicePreviewWidget* preview_after_ = nullptr;
    SlicePreviewWidget* preview_diff_ = nullptr;
    QComboBox* type_combo_ = nullptr;
    QDoubleSpinBox* f_low_spin_ = nullptr;
    QDoubleSpinBox* f_high_spin_ = nullptr;
    QSpinBox* order_spin_ = nullptr;
    QPushButton* apply_btn_ = nullptr;
    QPushButton* undo_btn_ = nullptr;
    QPushButton* apply_to_cube_btn_ = nullptr;
};

}  // namespace kubik
