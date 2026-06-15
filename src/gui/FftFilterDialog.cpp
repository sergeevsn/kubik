#include "FftFilterDialog.hpp"
#include "SpinBoxFix.hpp"
#include "SlicePreviewWidget.hpp"
#include "SpectrumPlotWidget.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace kubik {

namespace {

double estimateNyquistHz(float dt_ms) {
    if (dt_ms <= 0.f) {
        return 1.0;
    }
    return 500.0 / static_cast<double>(dt_ms);
}

}  // namespace

FftFilterDialog::FftFilterDialog(const std::vector<float>& slice, int w, int h, int h0, int h1, int v0,
                                 int v1, float dt_ms, SliceMode mode, QWidget* parent)
    : QDialog(parent),
      slice_(slice),
      w_(w),
      h_(h),
      h0_(h0),
      h1_(h1),
      v0_(v0),
      v1_(v1),
      dt_ms_(dt_ms),
      mode_(mode) {
    setWindowTitle(tr("FFT-фильтрация (1D)"));
    resize(980, 900);
    setupUi();
}

void FftFilterDialog::setupUi() {
    auto* root = new QVBoxLayout(this);

    spectrum_plot_ = new SpectrumPlotWidget(this);
    root->addWidget(spectrum_plot_, 2);

    auto* controls = new QGroupBox(tr("Параметры фильтра"), this);
    auto* grid = new QGridLayout(controls);

    type_combo_ = new QComboBox(controls);
    type_combo_->addItem(tr("Bandpass"), static_cast<int>(FftFilterType::Bandpass));
    type_combo_->addItem(tr("Low-pass"), static_cast<int>(FftFilterType::Lowpass));
    type_combo_->addItem(tr("High-pass"), static_cast<int>(FftFilterType::Highpass));
    type_combo_->addItem(tr("Notch"), static_cast<int>(FftFilterType::Notch));

    f_low_spin_ = new QDoubleSpinBox(controls);
    f_high_spin_ = new QDoubleSpinBox(controls);
    order_spin_ = new QSpinBox(controls);
    order_spin_->setRange(1, 12);
    order_spin_->setValue(4);

    const double nyq = estimateNyquistHz(dt_ms_);
    for (QDoubleSpinBox* spin : {f_low_spin_, f_high_spin_}) {
        spin->setDecimals(1);
        spin->setRange(0.1, nyq);
        spin->setSuffix(tr(" Hz"));
    }
    f_low_spin_->setValue(10.0);
    f_high_spin_->setValue(std::min(60.0, nyq * 0.8));

    for (QAbstractSpinBox* spin : {static_cast<QAbstractSpinBox*>(f_low_spin_),
                                    static_cast<QAbstractSpinBox*>(f_high_spin_),
                                    static_cast<QAbstractSpinBox*>(order_spin_)}) {
        setupSpinBox(spin);
    }

    grid->addWidget(new QLabel(tr("Тип"), controls), 0, 0);
    grid->addWidget(type_combo_, 0, 1);
    grid->addWidget(new QLabel(tr("F low"), controls), 1, 0);
    grid->addWidget(f_low_spin_, 1, 1);
    grid->addWidget(new QLabel(tr("F high"), controls), 2, 0);
    grid->addWidget(f_high_spin_, 2, 1);
    grid->addWidget(new QLabel(tr("Порядок Butterworth"), controls), 3, 0);
    grid->addWidget(order_spin_, 3, 1);

    auto* btn_row = new QHBoxLayout();
    apply_btn_ = new QPushButton(tr("Apply"), controls);
    undo_btn_ = new QPushButton(tr("Undo"), controls);
    apply_to_cube_btn_ = new QPushButton(tr("Apply To Cube"), controls);
    undo_btn_->setEnabled(false);
    btn_row->addWidget(apply_btn_);
    btn_row->addWidget(undo_btn_);
    btn_row->addWidget(apply_to_cube_btn_);
    grid->addLayout(btn_row, 4, 0, 1, 2);
    root->addWidget(controls);

    auto* previews = new QGroupBox(tr("Срез: до / после / разница"), this);
    auto* prev_row = new QHBoxLayout(previews);
    preview_before_ = new SlicePreviewWidget(previews);
    preview_after_ = new SlicePreviewWidget(previews);
    preview_diff_ = new SlicePreviewWidget(previews);
    preview_before_->setTitle(tr("До"));
    preview_after_->setTitle(tr("После"));
    preview_diff_->setTitle(tr("Разница"));
    prev_row->addWidget(preview_before_, 1);
    prev_row->addWidget(preview_after_, 1);
    prev_row->addWidget(preview_diff_, 1);
    for (SlicePreviewWidget* preview : {preview_before_, preview_after_, preview_diff_}) {
        preview->setMinimumHeight(200);
    }
    root->addWidget(previews, 2);

    spectrum_plot_->setFrequencyLimits(0.0, nyq);
    spectrum_plot_->setFrequencies(f_low_spin_->value(), f_high_spin_->value());
    spectrum_plot_->setFilterType(FftFilterType::Bandpass);

    connect(type_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &FftFilterDialog::onFilterTypeChanged);
    connect(f_low_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilterDialog::onFrequencySpinChanged);
    connect(f_high_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilterDialog::onFrequencySpinChanged);
    connect(spectrum_plot_, &SpectrumPlotWidget::frequenciesChanged, this,
            &FftFilterDialog::onPlotFrequenciesChanged);
    connect(apply_btn_, &QPushButton::clicked, this, &FftFilterDialog::onApply);
    connect(undo_btn_, &QPushButton::clicked, this, &FftFilterDialog::onUndo);
    connect(apply_to_cube_btn_, &QPushButton::clicked, this, &FftFilterDialog::onApplyToCube);

    original_spec_ = spectrumFromRegion(slice_);
    spectrum_plot_->setSpectrum(original_spec_.freqs_hz, original_spec_.amps);

    updateSpinVisibility();
    onApply();
}

Spectrum1D FftFilterDialog::spectrumFromRegion(const std::vector<float>& data) const {
    return computeAverageSpectrum1D(data, w_, h_, h0_, h1_, v0_, v1_, dt_ms_);
}

std::vector<float> FftFilterDialog::extractRegion(const std::vector<float>& data) const {
    const int rw = h1_ - h0_ + 1;
    const int rh = v1_ - v0_ + 1;
    std::vector<float> region(static_cast<std::size_t>(rw) * static_cast<std::size_t>(rh));
    for (int v = 0; v < rh; ++v) {
        for (int h = 0; h < rw; ++h) {
            region[static_cast<std::size_t>(v) * static_cast<std::size_t>(rw) + static_cast<std::size_t>(h)] =
                data[static_cast<std::size_t>(v0_ + v) * static_cast<std::size_t>(w_) +
                     static_cast<std::size_t>(h0_ + h)];
        }
    }
    return region;
}

void FftFilterDialog::updatePreviews(const std::vector<float>& before_region,
                                     const std::vector<float>& after_region) {
    const int rw = h1_ - h0_ + 1;
    const int rh = v1_ - v0_ + 1;

    std::vector<float> diff_region(before_region.size());
    for (std::size_t i = 0; i < diff_region.size(); ++i) {
        diff_region[i] = after_region[i] - before_region[i];
    }

    float vmin = before_region[0];
    float vmax = before_region[0];
    for (float v : before_region) {
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    for (float v : after_region) {
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    preview_vmin_ = vmin;
    preview_vmax_ = vmax;

    float dmin = diff_region[0];
    float dmax = diff_region[0];
    for (float v : diff_region) {
        dmin = std::min(dmin, v);
        dmax = std::max(dmax, v);
    }
    const float dabs = std::max(std::abs(dmin), std::abs(dmax));

    preview_before_->setSlice(before_region, rw, rh, preview_vmin_, preview_vmax_);
    preview_after_->setSlice(after_region, rw, rh, preview_vmin_, preview_vmax_);
    preview_diff_->setSlice(diff_region, rw, rh, -dabs, dabs);
}

FftFilterParams FftFilterDialog::currentParams() const {
    FftFilterParams p;
    p.type = static_cast<FftFilterType>(type_combo_->currentData().toInt());
    p.f_low_hz = f_low_spin_->value();
    p.f_high_hz = f_high_spin_->value();
    p.order = order_spin_->value();
    return p;
}

void FftFilterDialog::updateSpinVisibility() {
    const auto type = static_cast<FftFilterType>(type_combo_->currentData().toInt());
    spectrum_plot_->setFilterType(type);
    f_low_spin_->setEnabled(type == FftFilterType::Bandpass || type == FftFilterType::Highpass ||
                            type == FftFilterType::Notch);
    f_high_spin_->setEnabled(type == FftFilterType::Bandpass || type == FftFilterType::Lowpass ||
                             type == FftFilterType::Notch);
}

void FftFilterDialog::onFilterTypeChanged(int) {
    updateSpinVisibility();
    spectrum_plot_->setFilterType(static_cast<FftFilterType>(type_combo_->currentData().toInt()));
}

void FftFilterDialog::onFrequencySpinChanged() {
    if (f_low_spin_->value() > f_high_spin_->value()) {
        if (sender() == f_low_spin_) {
            f_high_spin_->setValue(f_low_spin_->value());
        } else {
            f_low_spin_->setValue(f_high_spin_->value());
        }
    }
    spectrum_plot_->setFrequencies(f_low_spin_->value(), f_high_spin_->value());
}

void FftFilterDialog::onPlotFrequenciesChanged(double f_low, double f_high) {
    f_low_spin_->blockSignals(true);
    f_high_spin_->blockSignals(true);
    f_low_spin_->setValue(f_low);
    f_high_spin_->setValue(f_high);
    f_low_spin_->blockSignals(false);
    f_high_spin_->blockSignals(false);
}

void FftFilterDialog::onApply() {
    filtered_slice_ = filterSliceRegion1D(slice_, w_, h_, h0_, h1_, v0_, v1_, dt_ms_, currentParams());

    const Spectrum1D spec_after = spectrumFromRegion(filtered_slice_);
    spectrum_plot_->setSpectrum(spec_after.freqs_hz, spec_after.amps);
    spectrum_plot_->clearFilteredSpectrum();
    spectrum_plot_->setFrequencies(f_low_spin_->value(), f_high_spin_->value());
    spectrum_plot_->setFilterType(static_cast<FftFilterType>(type_combo_->currentData().toInt()));

    updatePreviews(extractRegion(slice_), extractRegion(filtered_slice_));
    undo_btn_->setEnabled(true);
}

void FftFilterDialog::onUndo() {
    filtered_slice_ = slice_;
    spectrum_plot_->setSpectrum(original_spec_.freqs_hz, original_spec_.amps);
    spectrum_plot_->clearFilteredSpectrum();

    const std::vector<float> before_region = extractRegion(slice_);
    updatePreviews(before_region, before_region);
    undo_btn_->setEnabled(false);
}

void FftFilterDialog::onApplyToCube() {
    emit applyToCubeRequested(currentParams());
}

}  // namespace kubik
