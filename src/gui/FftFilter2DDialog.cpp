#include "FftFilter2DDialog.hpp"
#include "SpinBoxFix.hpp"
#include "kubik/AmplitudeClip.hpp"
#include "kubik/SegyCube.hpp"
#include "SlicePreviewWidget.hpp"
#include "Spectrum2DPlotWidget.hpp"

#include <QCheckBox>
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

FftFilter2DDialog::FftFilter2DDialog(const SegyCube* cube, int current_t_idx, CropBounds crop,
                                     ResampleParams resample, const std::vector<float>& slice, int w, int h,
                                     double d_xl, double d_il, ColorMap color_map, float clip_percent,
                                     QWidget* parent)
    : QDialog(parent),
      cube_(cube),
      current_t_idx_(current_t_idx),
      crop_(crop),
      resample_(resample),
      slice_(slice),
      w_(w),
      h_(h),
      d_xl_(std::max(d_xl, 1e-6)),
      d_il_(std::max(d_il, 1e-6)),
      color_map_(color_map),
      spectrum_clip_percent_(clip_percent),
      preview_clip_percent_(clip_percent) {
    setWindowTitle(tr("FFT footprint (2D)"));
    resize(980, 1020);
    setupUi();
}

void FftFilter2DDialog::setupUi() {
    auto* root = new QVBoxLayout(this);

    auto* spec_plots_row = new QHBoxLayout();
    spectrum_before_plot_ = new Spectrum2DPlotWidget(this);
    spectrum_after_plot_ = new Spectrum2DPlotWidget(this);
    spectrum_before_plot_->setClipPercent(spectrum_clip_percent_);
    spectrum_after_plot_->setClipPercent(spectrum_clip_percent_);
    spec_plots_row->addWidget(spectrum_before_plot_, 1);
    spec_plots_row->addWidget(spectrum_after_plot_, 1);
    root->addLayout(spec_plots_row, 3);

    auto* spec_clip_row = new QHBoxLayout();
    spec_clip_row->setContentsMargins(10, 0, 10, 4);
    spectrum_clip_spin_ = new QSpinBox(this);
    spectrum_clip_spin_->setRange(1, 100);
    spectrum_clip_spin_->setValue(static_cast<int>(spectrum_clip_percent_));
    spectrum_clip_spin_->setSuffix(QStringLiteral("%"));
    spectrum_clip_spin_->setToolTip(tr("Clip FK: 99 → [p0.5, p99.5], 1 → [p49.5, p50.5]"));
    setupSpinBox(spectrum_clip_spin_);
    spectrum_clip_range_label_ = new QLabel(this);
    spectrum_clip_range_label_->setMinimumWidth(160);
    mask_overlay_check_ = new QCheckBox(tr("Overlay mask"), this);
    mask_overlay_check_->setChecked(true);
    spec_clip_row->addWidget(new QLabel(tr("Clip FK:"), this));
    spec_clip_row->addWidget(spectrum_clip_spin_);
    spec_clip_row->addWidget(spectrum_clip_range_label_);
    spec_clip_row->addWidget(mask_overlay_check_);
    spec_clip_row->addStretch(1);
    root->addLayout(spec_clip_row);

    auto* controls = new QGroupBox(tr("Параметры footprint"), this);
    auto* grid = new QGridLayout(controls);

    notch_width_spin_ = new QDoubleSpinBox(controls);
    suppression_spin_ = new QDoubleSpinBox(controls);
    sensitivity_spin_ = new QDoubleSpinBox(controls);
    k_preserve_spin_ = new QDoubleSpinBox(controls);
    avg_count_spin_ = new QSpinBox(controls);
    notch_width_spin_->setDecimals(1);
    notch_width_spin_->setRange(1.0, 30.0);
    notch_width_spin_->setValue(6.0);
    suppression_spin_->setDecimals(2);
    suppression_spin_->setRange(0.0, 1.0);
    suppression_spin_->setSingleStep(0.05);
    suppression_spin_->setValue(1.0);
    sensitivity_spin_->setDecimals(2);
    sensitivity_spin_->setRange(0.5, 5.0);
    sensitivity_spin_->setValue(2.0);
    k_preserve_spin_->setDecimals(1);
    k_preserve_spin_->setRange(0.0, 200.0);
    k_preserve_spin_->setValue(5.0);
    avg_count_spin_->setRange(1, 500);
    avg_count_spin_->setValue(50);

    for (QAbstractSpinBox* spin : {static_cast<QAbstractSpinBox*>(notch_width_spin_),
                                   static_cast<QAbstractSpinBox*>(suppression_spin_),
                                   static_cast<QAbstractSpinBox*>(sensitivity_spin_),
                                   static_cast<QAbstractSpinBox*>(k_preserve_spin_),
                                   static_cast<QAbstractSpinBox*>(avg_count_spin_)}) {
        setupSpinBox(spin);
    }

    grid->addWidget(new QLabel(tr("Ширина фильтра (град)"), controls), 0, 0);
    grid->addWidget(notch_width_spin_, 0, 1);
    grid->addWidget(new QLabel(tr("Сила подавления"), controls), 1, 0);
    grid->addWidget(suppression_spin_, 1, 1);
    grid->addWidget(new QLabel(tr("Чувствительность"), controls), 2, 0);
    grid->addWidget(sensitivity_spin_, 2, 1);
    grid->addWidget(new QLabel(tr("Сохранение центра k (px)"), controls), 3, 0);
    grid->addWidget(k_preserve_spin_, 3, 1);
    grid->addWidget(new QLabel(tr("Слайсов для осреднения"), controls), 4, 0);
    grid->addWidget(avg_count_spin_, 4, 1);

    auto* hint = new QLabel(
        tr("Осреднение берётся по окну time-слайсов вокруг текущего.\n"
           "Маска вычисляется по среднему спектру и применяется к текущему слайсу/кубу."),
        controls);
    hint->setWordWrap(true);
    grid->addWidget(hint, 5, 0, 1, 2);

    auto* btn_row = new QHBoxLayout();
    undo_btn_ = new QPushButton(tr("Undo"), controls);
    apply_to_cube_btn_ = new QPushButton(tr("Apply To Cube"), controls);
    undo_btn_->setEnabled(false);
    btn_row->addWidget(undo_btn_);
    btn_row->addWidget(apply_to_cube_btn_);
    grid->addLayout(btn_row, 6, 0, 1, 2);
    root->addWidget(controls);

    auto* previews = new QGroupBox(tr("Срез: до / после / разница"), this);
    auto* prev_layout = new QVBoxLayout(previews);
    auto* prev_clip_row = new QHBoxLayout();
    preview_clip_spin_ = new QSpinBox(previews);
    preview_clip_spin_->setRange(1, 100);
    preview_clip_spin_->setValue(static_cast<int>(preview_clip_percent_));
    preview_clip_spin_->setSuffix(QStringLiteral("%"));
    preview_clip_spin_->setToolTip(
        tr("Clip по до/после; удалено = clipped(до) − clipped(после), отображение ±span"));
    setupSpinBox(preview_clip_spin_);
    preview_clip_range_label_ = new QLabel(previews);
    preview_clip_range_label_->setMinimumWidth(160);
    prev_clip_row->addWidget(new QLabel(tr("Clip:"), previews));
    prev_clip_row->addWidget(preview_clip_spin_);
    prev_clip_row->addWidget(preview_clip_range_label_);
    prev_clip_row->addStretch(1);
    prev_layout->addLayout(prev_clip_row);

    auto* prev_row = new QHBoxLayout();
    preview_before_ = new SlicePreviewWidget(previews);
    preview_after_ = new SlicePreviewWidget(previews);
    preview_diff_ = new SlicePreviewWidget(previews);
    preview_before_->setTitle(tr("До"));
    preview_after_->setTitle(tr("После"));
    preview_diff_->setTitle(tr("Удалено"));
    prev_row->addWidget(preview_before_, 1);
    prev_row->addWidget(preview_after_, 1);
    prev_row->addWidget(preview_diff_, 1);
    for (SlicePreviewWidget* preview : {preview_before_, preview_after_, preview_diff_}) {
        preview->setMinimumHeight(200);
        preview->setColorMap(color_map_);
    }
    prev_layout->addLayout(prev_row, 1);
    root->addWidget(previews, 1);

    connect(spectrum_clip_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
            &FftFilter2DDialog::onSpectrumClipChanged);
    connect(preview_clip_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
            &FftFilter2DDialog::onPreviewClipChanged);
    connect(notch_width_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilter2DDialog::onParamsChanged);
    connect(suppression_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilter2DDialog::onParamsChanged);
    connect(sensitivity_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilter2DDialog::onParamsChanged);
    connect(k_preserve_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilter2DDialog::onParamsChanged);
    connect(avg_count_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
            &FftFilter2DDialog::onParamsChanged);
    connect(undo_btn_, &QPushButton::clicked, this, &FftFilter2DDialog::onUndo);
    connect(apply_to_cube_btn_, &QPushButton::clicked, this, &FftFilter2DDialog::onApplyToCube);
    connect(mask_overlay_check_, &QCheckBox::toggled, this, &FftFilter2DDialog::onMaskOverlayToggled);

    original_spec_ = averageSpectrum(avg_count_spin_->value());
    spectrum_before_plot_->setSpectrum(original_spec_);
    spectrum_after_plot_->setSpectrum(original_spec_);
    updateSpectrumClipLabel();
    recompute();
}

Spectrum2D FftFilter2DDialog::averageSpectrum(int avg_count) const {
    Spectrum2D out;
    out.w = w_;
    out.h = h_;
    if (!cube_ || w_ <= 0 || h_ <= 0) {
        return computeSpectrum2D(slice_, w_, h_, d_xl_, d_il_);
    }
    const int n_t = cube_->geometry().n_t;
    if (n_t <= 0) {
        return computeSpectrum2D(slice_, w_, h_, d_xl_, d_il_);
    }
    const int count = std::max(1, avg_count);
    const int half = count / 2;
    const int t0 = std::max(0, current_t_idx_ - half);
    const int t1 = std::min(n_t - 1, current_t_idx_ + count - half - 1);
    std::vector<double> sum(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), 0.0);
    int used = 0;
    for (int t = t0; t <= t1; ++t) {
        int sw = 0;
        int sh = 0;
        std::vector<int32_t> xl;
        std::vector<int32_t> il;
        const std::vector<float> s = cube_->readTimeSliceProcessed(t, crop_, resample_, sw, sh, xl, il);
        if (sw != w_ || sh != h_ || static_cast<int>(s.size()) != w_ * h_) {
            continue;
        }
        const Spectrum2D spec = computeSpectrum2D(s, w_, h_, d_xl_, d_il_);
        if (static_cast<int>(spec.amps.size()) != w_ * h_) {
            continue;
        }
        for (std::size_t i = 0; i < sum.size(); ++i) {
            sum[i] += spec.amps[i];
        }
        ++used;
    }
    if (used <= 0) {
        return computeSpectrum2D(slice_, w_, h_, d_xl_, d_il_);
    }
    out = computeSpectrum2D(slice_, w_, h_, d_xl_, d_il_);
    out.amps.resize(sum.size());
    for (std::size_t i = 0; i < sum.size(); ++i) {
        out.amps[i] = sum[i] / static_cast<double>(used);
    }
    return out;
}

void FftFilter2DDialog::updateSpectrumClipLabel() {
    if (!spectrum_clip_range_label_) {
        return;
    }
    spectrum_clip_range_label_->setText(
        tr("[%1, %2]")
            .arg(spectrum_before_plot_->ampClipMin(), 0, 'g', 4)
            .arg(spectrum_before_plot_->ampClipMax(), 0, 'g', 4));
}

void FftFilter2DDialog::updatePreviewClipLabel() {
    if (!preview_clip_range_label_) {
        return;
    }
    preview_clip_range_label_->setText(
        tr("[%1, %2]")
            .arg(static_cast<double>(preview_vmin_), 0, 'g', 4)
            .arg(static_cast<double>(preview_vmax_), 0, 'g', 4));
}

void FftFilter2DDialog::refreshPreviews() {
    if (preview_before_data_.empty() || preview_after_data_.empty()) {
        return;
    }
    applyPreviewDisplay();
}

void FftFilter2DDialog::applyPreviewDisplay() {
    const int rw = w_;
    const int rh = h_;
    if (preview_before_data_.empty() || preview_after_data_.empty() || rw <= 0 || rh <= 0) {
        return;
    }

    std::vector<float> amp_pooled;
    amp_pooled.reserve(preview_before_data_.size() + preview_after_data_.size());
    amp_pooled.insert(amp_pooled.end(), preview_before_data_.begin(), preview_before_data_.end());
    amp_pooled.insert(amp_pooled.end(), preview_after_data_.begin(), preview_after_data_.end());
    amplitudeClipRange(amp_pooled, preview_clip_percent_, preview_vmin_, preview_vmax_);
    if (!(preview_vmax_ > preview_vmin_)) {
        preview_vmax_ = preview_vmin_ + 1.f;
    }

    const float span = preview_vmax_ - preview_vmin_;
    preview_diff_data_.resize(preview_before_data_.size());
    for (std::size_t i = 0; i < preview_diff_data_.size(); ++i) {
        const float b = std::clamp(preview_before_data_[i], preview_vmin_, preview_vmax_);
        const float a = std::clamp(preview_after_data_[i], preview_vmin_, preview_vmax_);
        preview_diff_data_[i] = b - a;
    }

    updatePreviewClipLabel();

    preview_before_->setSlice(preview_before_data_, rw, rh, preview_vmin_, preview_vmax_);
    preview_after_->setSlice(preview_after_data_, rw, rh, preview_vmin_, preview_vmax_);
    preview_diff_->setSlice(preview_diff_data_, rw, rh, -span, span);
}

void FftFilter2DDialog::onSpectrumClipChanged(int value) {
    spectrum_clip_percent_ = static_cast<float>(value);
    spectrum_before_plot_->setClipPercent(spectrum_clip_percent_);
    spectrum_after_plot_->setClipPercent(spectrum_clip_percent_);
    updateSpectrumClipLabel();
}

void FftFilter2DDialog::onPreviewClipChanged(int value) {
    preview_clip_percent_ = static_cast<float>(value);
    refreshPreviews();
}

void FftFilter2DDialog::updatePreviews(const std::vector<float>& before_region,
                                       const std::vector<float>& after_region) {
    preview_before_data_ = before_region;
    preview_after_data_ = after_region;
    applyPreviewDisplay();
}

FftFilter2DParams FftFilter2DDialog::currentParams() const {
    FftFilter2DParams p;
    p.notch_width_deg = notch_width_spin_->value();
    p.suppression = suppression_spin_->value();
    p.sensitivity = sensitivity_spin_->value();
    p.k_preserve = k_preserve_spin_->value();
    p.avg_slice_count = avg_count_spin_->value();
    p.mask_w = w_;
    p.mask_h = h_;
    p.mask = buildFootprintMask2D(original_spec_, p);
    return p;
}

void FftFilter2DDialog::onParamsChanged() {
    original_spec_ = averageSpectrum(avg_count_spin_->value());
    recompute();
}

void FftFilter2DDialog::recompute() {
    const FftFilter2DParams p = currentParams();
    filtered_slice_ = filterSlice2DWithMask(slice_, w_, h_, p.mask);
    spectrum_before_plot_->setSpectrum(original_spec_);
    Spectrum2D spec_after = original_spec_;
    if (spec_after.w == p.mask_w && spec_after.h == p.mask_h &&
        static_cast<int>(spec_after.amps.size()) == p.mask_w * p.mask_h) {
        for (std::size_t i = 0; i < spec_after.amps.size(); ++i) {
            spec_after.amps[i] *= static_cast<double>(p.mask[i]);
        }
    }
    spectrum_after_plot_->setSpectrum(spec_after);
    spectrum_before_plot_->setMaskOverlay(p.mask, p.mask_w, p.mask_h);
    spectrum_after_plot_->setMaskOverlay(p.mask, p.mask_w, p.mask_h);
    onMaskOverlayToggled(mask_overlay_check_ && mask_overlay_check_->isChecked());
    updateSpectrumClipLabel();
    updatePreviews(slice_, filtered_slice_);
    undo_btn_->setEnabled(true);
}

void FftFilter2DDialog::onUndo() {
    filtered_slice_ = slice_;
    spectrum_before_plot_->setSpectrum(original_spec_);
    spectrum_after_plot_->setSpectrum(original_spec_);
    updateSpectrumClipLabel();
    updatePreviews(slice_, filtered_slice_);
    undo_btn_->setEnabled(false);
}

void FftFilter2DDialog::onApplyToCube() {
    emit applyToCubeRequested(currentParams());
}

void FftFilter2DDialog::onMaskOverlayToggled(bool enabled) {
    if (spectrum_before_plot_) {
        spectrum_before_plot_->setMaskOverlayEnabled(enabled);
    }
    if (spectrum_after_plot_) {
        spectrum_after_plot_->setMaskOverlayEnabled(enabled);
    }
}

}  // namespace kubik
