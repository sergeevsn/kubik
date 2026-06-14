#include "FftFilter2DDialog.hpp"
#include "kubik/AmplitudeClip.hpp"
#include "SlicePreviewWidget.hpp"
#include "Spectrum2DPlotWidget.hpp"

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

double nyquistK(double d) {
    if (d <= 0.0) {
        return 0.5;
    }
    return 0.5 / d;
}

}  // namespace

FftFilter2DDialog::FftFilter2DDialog(const std::vector<float>& slice, int w, int h, int h0, int h1, int v0,
                                     int v1, double d_xl, double d_il, ColorMap color_map, float clip_percent,
                                     QWidget* parent)
    : QDialog(parent),
      slice_(slice),
      w_(w),
      h_(h),
      h0_(h0),
      h1_(h1),
      v0_(v0),
      v1_(v1),
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

    spectrum_plot_ = new Spectrum2DPlotWidget(this);
    spectrum_plot_->setClipPercent(spectrum_clip_percent_);
    root->addWidget(spectrum_plot_, 3);

    auto* spec_clip_row = new QHBoxLayout();
    spec_clip_row->setContentsMargins(10, 0, 10, 4);
    spectrum_clip_spin_ = new QSpinBox(this);
    spectrum_clip_spin_->setRange(1, 100);
    spectrum_clip_spin_->setValue(static_cast<int>(spectrum_clip_percent_));
    spectrum_clip_spin_->setSuffix(QStringLiteral("%"));
    spectrum_clip_spin_->setToolTip(tr("Clip FK: 99 → [p0.5, p99.5], 1 → [p49.5, p50.5]"));
    spectrum_clip_range_label_ = new QLabel(this);
    spectrum_clip_range_label_->setMinimumWidth(160);
    spec_clip_row->addWidget(new QLabel(tr("Clip FK:"), this));
    spec_clip_row->addWidget(spectrum_clip_spin_);
    spec_clip_row->addWidget(spectrum_clip_range_label_);
    spec_clip_row->addStretch(1);
    root->addLayout(spec_clip_row);

    auto* controls = new QGroupBox(tr("Параметры footprint"), this);
    auto* grid = new QGridLayout(controls);

    type_combo_ = new QComboBox(controls);
    type_combo_->addItem(tr("Footprint IL-XL"), static_cast<int>(FftFilter2DType::FootprintIlXl));
    type_combo_->addItem(tr("Footprint IL"), static_cast<int>(FftFilter2DType::FootprintIl));
    type_combo_->addItem(tr("Footprint XL"), static_cast<int>(FftFilter2DType::FootprintXl));

    const double k_nyq_il = nyquistK(d_il_);
    const double k_nyq_xl = nyquistK(d_xl_);
    const double k_nyq = std::min(k_nyq_il, k_nyq_xl);

    k_cut_il_spin_ = new QDoubleSpinBox(controls);
    k_cut_xl_spin_ = new QDoubleSpinBox(controls);
    k_pass_spin_ = new QDoubleSpinBox(controls);
    k_smooth_spin_ = new QDoubleSpinBox(controls);

    for (QDoubleSpinBox* spin : {k_cut_il_spin_, k_cut_xl_spin_}) {
        spin->setDecimals(4);
        spin->setRange(0.0001, std::max(k_nyq_il, k_nyq_xl));
        spin->setSingleStep(k_nyq * 0.05);
    }
    k_pass_spin_->setDecimals(4);
    k_pass_spin_->setRange(0.0, std::max(k_nyq_il, k_nyq_xl));
    k_pass_spin_->setSingleStep(k_nyq * 0.01);
    k_smooth_spin_->setDecimals(4);
    k_smooth_spin_->setRange(0.0, std::max(k_nyq_il, k_nyq_xl));
    k_smooth_spin_->setSingleStep(k_nyq * 0.01);

    k_cut_il_spin_->setValue(std::min(k_nyq_il * 0.25, 0.05));
    k_cut_xl_spin_->setValue(std::min(k_nyq_xl * 0.25, 0.05));
    k_pass_spin_->setValue(std::min(k_cut_il_spin_->value() * 0.2, k_nyq * 0.02));
    {
        const double band = std::min(k_cut_il_spin_->value(), k_cut_xl_spin_->value()) - k_pass_spin_->value();
        k_smooth_spin_->setValue(std::max(0.0, band * 0.5));
    }

    grid->addWidget(new QLabel(tr("Режим"), controls), 0, 0);
    grid->addWidget(type_combo_, 0, 1);
    grid->addWidget(new QLabel(tr("k cut IL"), controls), 1, 0);
    grid->addWidget(k_cut_il_spin_, 1, 1);
    grid->addWidget(new QLabel(tr("k cut XL"), controls), 2, 0);
    grid->addWidget(k_cut_xl_spin_, 2, 1);
    grid->addWidget(new QLabel(tr("k pass"), controls), 3, 0);
    grid->addWidget(k_pass_spin_, 3, 1);
    grid->addWidget(new QLabel(tr("k smooth"), controls), 4, 0);
    grid->addWidget(k_smooth_spin_, 4, 1);

    auto* hint = new QLabel(
        tr("Зелёные линии — узкая полоса пропускания |k|<k_pass.\n"
           "Оранжевые — граница вырезания |k|=k_cut.\n"
           "k smooth — ширина косинусного сглаживания на каждом краю зоны вырезания.\n"
           "Перетаскивание линий мышью (ЛКМ) меняет k_pass и k_cut."),
        controls);
    hint->setWordWrap(true);
    grid->addWidget(hint, 5, 0, 1, 2);

    auto* btn_row = new QHBoxLayout();
    apply_btn_ = new QPushButton(tr("Apply"), controls);
    undo_btn_ = new QPushButton(tr("Undo"), controls);
    apply_to_cube_btn_ = new QPushButton(tr("Apply To Cube"), controls);
    undo_btn_->setEnabled(false);
    btn_row->addWidget(apply_btn_);
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
        tr("Clip по до/после; разница = clipped(после) − clipped(до), отображение ±span"));
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
    preview_diff_->setTitle(tr("Разница"));
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
    connect(spectrum_plot_, &Spectrum2DPlotWidget::filterParamsChanged, this,
            &FftFilter2DDialog::onPlotFilterParamsChanged);
    connect(type_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &FftFilter2DDialog::onFilterTypeChanged);
    connect(k_cut_il_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilter2DDialog::onParamsChanged);
    connect(k_cut_xl_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilter2DDialog::onParamsChanged);
    connect(k_pass_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilter2DDialog::onParamsChanged);
    connect(k_smooth_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &FftFilter2DDialog::onParamsChanged);
    connect(apply_btn_, &QPushButton::clicked, this, &FftFilter2DDialog::onApply);
    connect(undo_btn_, &QPushButton::clicked, this, &FftFilter2DDialog::onUndo);
    connect(apply_to_cube_btn_, &QPushButton::clicked, this, &FftFilter2DDialog::onApplyToCube);

    original_spec_ = spectrumFromRegion(slice_);
    spectrum_plot_->setSpectrum(original_spec_);
    updateSpectrumClipLabel();
    updateSpinVisibility();
    onApply();
}

Spectrum2D FftFilter2DDialog::spectrumFromRegion(const std::vector<float>& data) const {
    const std::vector<float> region = extractRegion(data);
    const int rw = h1_ - h0_ + 1;
    const int rh = v1_ - v0_ + 1;
    return computeSpectrum2D(region, rw, rh, d_xl_, d_il_);
}

std::vector<float> FftFilter2DDialog::extractRegion(const std::vector<float>& data) const {
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

void FftFilter2DDialog::updateSpectrumClipLabel() {
    if (!spectrum_clip_range_label_) {
        return;
    }
    spectrum_clip_range_label_->setText(
        tr("[%1, %2]")
            .arg(spectrum_plot_->ampClipMin(), 0, 'g', 4)
            .arg(spectrum_plot_->ampClipMax(), 0, 'g', 4));
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
    const int rw = h1_ - h0_ + 1;
    const int rh = v1_ - v0_ + 1;
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
        preview_diff_data_[i] = a - b;
    }

    updatePreviewClipLabel();

    preview_before_->setSlice(preview_before_data_, rw, rh, preview_vmin_, preview_vmax_);
    preview_after_->setSlice(preview_after_data_, rw, rh, preview_vmin_, preview_vmax_);
    preview_diff_->setSlice(preview_diff_data_, rw, rh, -span, span);
}

void FftFilter2DDialog::onSpectrumClipChanged(int value) {
    spectrum_clip_percent_ = static_cast<float>(value);
    spectrum_plot_->setClipPercent(spectrum_clip_percent_);
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
    p.type = static_cast<FftFilter2DType>(type_combo_->currentData().toInt());
    p.k_cut_il = k_cut_il_spin_->value();
    p.k_cut_xl = k_cut_xl_spin_->value();
    p.k_pass = k_pass_spin_->value();
    p.k_smooth = k_smooth_spin_->value();
    if (p.k_pass > p.k_cut_il) {
        p.k_pass = p.k_cut_il;
    }
    if (p.k_pass > p.k_cut_xl) {
        p.k_pass = std::min(p.k_pass, p.k_cut_xl);
    }
    const double min_cut = std::min(p.k_cut_il, p.k_cut_xl);
    if (p.k_smooth > (min_cut - p.k_pass) * 0.5) {
        p.k_smooth = std::max(0.0, (min_cut - p.k_pass) * 0.5);
    }
    return p;
}

void FftFilter2DDialog::updateSpinVisibility() {
    const auto type = static_cast<FftFilter2DType>(type_combo_->currentData().toInt());
    k_cut_il_spin_->setEnabled(type == FftFilter2DType::FootprintIl ||
                                type == FftFilter2DType::FootprintIlXl);
    k_cut_xl_spin_->setEnabled(type == FftFilter2DType::FootprintXl ||
                               type == FftFilter2DType::FootprintIlXl);
    spectrum_plot_->setFilterParams(currentParams());
}

void FftFilter2DDialog::onFilterTypeChanged(int) {
    updateSpinVisibility();
}

void FftFilter2DDialog::onPlotFilterParamsChanged(const FftFilter2DParams& params) {
    k_cut_il_spin_->blockSignals(true);
    k_cut_xl_spin_->blockSignals(true);
    k_pass_spin_->blockSignals(true);
    k_smooth_spin_->blockSignals(true);
    k_cut_il_spin_->setValue(params.k_cut_il);
    k_cut_xl_spin_->setValue(params.k_cut_xl);
    k_pass_spin_->setValue(params.k_pass);
    k_smooth_spin_->setValue(params.k_smooth);
    k_cut_il_spin_->blockSignals(false);
    k_cut_xl_spin_->blockSignals(false);
    k_pass_spin_->blockSignals(false);
    k_smooth_spin_->blockSignals(false);
    spectrum_plot_->setFilterParams(params);
}

void FftFilter2DDialog::onParamsChanged() {
    if (k_pass_spin_->value() > k_cut_il_spin_->value()) {
        k_pass_spin_->blockSignals(true);
        k_pass_spin_->setValue(k_cut_il_spin_->value());
        k_pass_spin_->blockSignals(false);
    }
    if (k_pass_spin_->value() > k_cut_xl_spin_->value()) {
        k_pass_spin_->blockSignals(true);
        k_pass_spin_->setValue(k_cut_xl_spin_->value());
        k_pass_spin_->blockSignals(false);
    }
    const double min_cut = std::min(k_cut_il_spin_->value(), k_cut_xl_spin_->value());
    const double max_smooth = std::max(0.0, (min_cut - k_pass_spin_->value()) * 0.5);
    if (k_smooth_spin_->value() > max_smooth) {
        k_smooth_spin_->blockSignals(true);
        k_smooth_spin_->setValue(max_smooth);
        k_smooth_spin_->blockSignals(false);
    }
    spectrum_plot_->setFilterParams(currentParams());
}

void FftFilter2DDialog::onApply() {
    filtered_slice_ =
        filterSliceRegion2D(slice_, w_, h_, h0_, h1_, v0_, v1_, d_xl_, d_il_, currentParams());

    const Spectrum2D spec_after = spectrumFromRegion(filtered_slice_);
    spectrum_plot_->setSpectrum(spec_after);
    spectrum_plot_->setFilterParams(currentParams());
    updateSpectrumClipLabel();

    updatePreviews(extractRegion(slice_), extractRegion(filtered_slice_));
    undo_btn_->setEnabled(true);
}

void FftFilter2DDialog::onUndo() {
    filtered_slice_ = slice_;
    spectrum_plot_->setSpectrum(original_spec_);
    spectrum_plot_->setFilterParams(currentParams());
    updateSpectrumClipLabel();

    updatePreviews(extractRegion(slice_), extractRegion(filtered_slice_));
    undo_btn_->setEnabled(false);
}

void FftFilter2DDialog::onApplyToCube() {
    emit applyToCubeRequested(currentParams());
}

}  // namespace kubik
