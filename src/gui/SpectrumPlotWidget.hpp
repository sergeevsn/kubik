#pragma once

#include <QWidget>

#include <vector>

#include "kubik/FftFilter.hpp"

namespace kubik {

/// График амплитудного спектра с интерактивными линиями частот.
class SpectrumPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumPlotWidget(QWidget* parent = nullptr);

    void setSpectrum(const std::vector<double>& freqs, const std::vector<double>& amps);
    void setFilteredSpectrum(const std::vector<double>& freqs, const std::vector<double>& amps);
    void clearFilteredSpectrum();

    void setFilterType(FftFilterType type);
    void setFrequencies(double f_low_hz, double f_high_hz);
    void setFrequencyLimits(double f_min_hz, double f_max_hz);

signals:
    void frequenciesChanged(double f_low_hz, double f_high_hz);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRectF plotRect() const;
    double freqAtX(int px) const;
    int xAtFreq(double freq) const;
    enum class DragTarget { None, Low, High };
    DragTarget hitTest(int px) const;

    std::vector<double> freqs_;
    std::vector<double> amps_;
    std::vector<double> filt_freqs_;
    std::vector<double> filt_amps_;
    FftFilterType filter_type_ = FftFilterType::Bandpass;
    double f_low_hz_ = 10.0;
    double f_high_hz_ = 60.0;
    double f_min_hz_ = 0.0;
    double f_max_hz_ = 125.0;
    DragTarget drag_ = DragTarget::None;
};

}  // namespace kubik
