#include "SpectrumPlotWidget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace kubik {

namespace {

constexpr int kLeft = 56;
constexpr int kRight = 16;
constexpr int kTop = 18;
constexpr int kBottom = 34;
constexpr int kHitPx = 8;

double chooseTickStep(double span, int max_ticks) {
    if (span <= 0.0 || max_ticks <= 0) {
        return 1.0;
    }
    const double raw = span / static_cast<double>(max_ticks);
    const double candidates[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000};
    for (double c : candidates) {
        if (c >= raw) {
            return c;
        }
    }
    return raw;
}

QString formatTickValue(double value, double span) {
    if (span >= 100.0) {
        return QString::number(value, 'f', 0);
    }
    if (span >= 10.0) {
        return QString::number(value, 'f', 1);
    }
    return QString::number(value, 'g', 3);
}

}  // namespace

SpectrumPlotWidget::SpectrumPlotWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(180);
    setMouseTracking(true);
}

void SpectrumPlotWidget::setSpectrum(const std::vector<double>& freqs, const std::vector<double>& amps) {
    freqs_ = freqs;
    amps_ = amps;
    if (!freqs_.empty()) {
        f_max_hz_ = freqs_.back();
    }
    update();
}

void SpectrumPlotWidget::setFilteredSpectrum(const std::vector<double>& freqs,
                                             const std::vector<double>& amps) {
    filt_freqs_ = freqs;
    filt_amps_ = amps;
    update();
}

void SpectrumPlotWidget::clearFilteredSpectrum() {
    filt_freqs_.clear();
    filt_amps_.clear();
    update();
}

void SpectrumPlotWidget::setFilterType(FftFilterType type) {
    filter_type_ = type;
    update();
}

void SpectrumPlotWidget::setFrequencies(double f_low_hz, double f_high_hz) {
    f_low_hz_ = f_low_hz;
    f_high_hz_ = f_high_hz;
    if (f_low_hz_ > f_high_hz_) {
        std::swap(f_low_hz_, f_high_hz_);
    }
    update();
}

void SpectrumPlotWidget::setFrequencyLimits(double f_min_hz, double f_max_hz) {
    f_min_hz_ = f_min_hz;
    f_max_hz_ = std::max(f_max_hz, f_min_hz + 1.0);
    update();
}

QRectF SpectrumPlotWidget::plotRect() const {
    return QRectF(kLeft, kTop,
                  std::max(1.0, static_cast<double>(width() - kLeft - kRight)),
                  std::max(1.0, static_cast<double>(height() - kTop - kBottom)));
}

double SpectrumPlotWidget::freqAtX(int px) const {
    const QRectF plot = plotRect();
    if (plot.width() <= 1.0) {
        return f_min_hz_;
    }
    const double rel = (static_cast<double>(px) - plot.left()) / plot.width();
    return f_min_hz_ + rel * (f_max_hz_ - f_min_hz_);
}

int SpectrumPlotWidget::xAtFreq(double freq) const {
    const QRectF plot = plotRect();
    const double span = std::max(f_max_hz_ - f_min_hz_, 1e-9);
    const double rel = (freq - f_min_hz_) / span;
    return static_cast<int>(plot.left() + rel * plot.width() + 0.5);
}

SpectrumPlotWidget::DragTarget SpectrumPlotWidget::hitTest(int px) const {
    const int x_low = xAtFreq(f_low_hz_);
    const int x_high = xAtFreq(f_high_hz_);
    if (std::abs(px - x_low) <= kHitPx) {
        return DragTarget::Low;
    }
    if (std::abs(px - x_high) <= kHitPx) {
        return DragTarget::High;
    }
    return DragTarget::None;
}

void SpectrumPlotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().brush(QPalette::Window));

    const QRectF plot = plotRect();

    double max_amp = 0.0;
    if (!amps_.empty()) {
        for (double a : amps_) {
            max_amp = std::max(max_amp, a);
        }
    }
    if (!filt_amps_.empty()) {
        for (double a : filt_amps_) {
            max_amp = std::max(max_amp, a);
        }
    }
    if (max_amp <= 0.0) {
        max_amp = 1.0;
    }

    QFont axis_font = p.font();
    axis_font.setPointSize(8);
    p.setFont(axis_font);
    p.setPen(palette().color(QPalette::Text));

    const double f_span = std::max(f_max_hz_ - f_min_hz_, 1e-9);
    const double f_step = chooseTickStep(f_span, 6);
    const double f_start = std::ceil(f_min_hz_ / f_step) * f_step;
    for (double f = f_start; f <= f_max_hz_ + 1e-6; f += f_step) {
        const int x = xAtFreq(f);
        p.drawLine(x, static_cast<int>(plot.bottom()), x, static_cast<int>(plot.bottom()) + 4);
        const QString label = formatTickValue(f, f_span);
        p.drawText(QRect(x - 28, static_cast<int>(plot.bottom()) + 4, 56, 14), Qt::AlignHCenter | Qt::AlignTop,
                   label);
    }

    const double a_step = chooseTickStep(max_amp, 5);
    for (double a = 0.0; a <= max_amp + 1e-9; a += a_step) {
        const double rel = a / max_amp;
        const int y = static_cast<int>(plot.bottom() - rel * plot.height());
        p.drawLine(static_cast<int>(plot.left()) - 4, y, static_cast<int>(plot.left()), y);
        const QString label = formatTickValue(a, max_amp);
        p.drawText(QRect(0, y - 7, kLeft - 6, 14), Qt::AlignRight | Qt::AlignVCenter, label);
    }

    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(plot);

    if (!freqs_.empty() && !amps_.empty()) {
        p.setRenderHint(QPainter::Antialiasing, true);

        auto drawCurve = [&](const std::vector<double>& fx, const std::vector<double>& fy,
                             const QColor& color, Qt::PenStyle style, bool fill_under) {
            if (fx.size() < 2 || fy.size() != fx.size()) {
                return;
            }
            QPainterPath path;
            bool started = false;
            double x_first = 0.0;
            double x_last = 0.0;
            for (std::size_t i = 0; i < fx.size(); ++i) {
                const double rel = (fx[i] - f_min_hz_) / std::max(f_max_hz_ - f_min_hz_, 1e-9);
                if (rel < 0.0 || rel > 1.0) {
                    continue;
                }
                const double x = plot.left() + rel * plot.width();
                const double y = plot.bottom() - (fy[i] / max_amp) * plot.height();
                if (!started) {
                    path.moveTo(x, y);
                    x_first = x;
                    started = true;
                } else {
                    path.lineTo(x, y);
                }
                x_last = x;
            }
            if (!started) {
                return;
            }
            if (fill_under) {
                QPainterPath fill = path;
                fill.lineTo(x_last, plot.bottom());
                fill.lineTo(x_first, plot.bottom());
                fill.closeSubpath();
                p.setPen(Qt::NoPen);
                p.fillPath(fill, QColor(220, 60, 60, 100));
            }
            QPen pen(color, 1.6, style);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        };

        drawCurve(freqs_, amps_, QColor(100, 170, 255), Qt::SolidLine, true);
        if (!filt_amps_.empty()) {
            drawCurve(filt_freqs_, filt_amps_, QColor(255, 170, 80), Qt::SolidLine, false);
        }

        auto drawFreqLine = [&](double freq, const QColor& color) {
            const int x = xAtFreq(freq);
            QPen pen(color, 1.5, Qt::DashLine);
            p.setPen(pen);
            p.drawLine(x, static_cast<int>(plot.top()), x, static_cast<int>(plot.bottom()));
        };

        switch (filter_type_) {
        case FftFilterType::Bandpass:
        case FftFilterType::Notch:
            drawFreqLine(f_low_hz_, QColor(255, 120, 80));
            drawFreqLine(f_high_hz_, QColor(80, 220, 140));
            break;
        case FftFilterType::Lowpass:
            drawFreqLine(f_high_hz_, QColor(80, 220, 140));
            break;
        case FftFilterType::Highpass:
            drawFreqLine(f_low_hz_, QColor(255, 120, 80));
            break;
        }
    }

    p.setPen(palette().color(QPalette::Text));
    QFont title_font = axis_font;
    title_font.setBold(true);
    p.setFont(title_font);
    p.drawText(QRectF(plot.left(), height() - 18, plot.width(), 16), Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("Frequency, Hz"));
    p.save();
    p.translate(12, plot.top() + plot.height() / 2.0);
    p.rotate(-90.0);
    p.drawText(QRect(-50, -8, 100, 16), Qt::AlignCenter, QStringLiteral("Amplitude"));
    p.restore();
}

void SpectrumPlotWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        drag_ = hitTest(event->pos().x());
    }
    QWidget::mousePressEvent(event);
}

void SpectrumPlotWidget::mouseMoveEvent(QMouseEvent* event) {
    if (drag_ != DragTarget::None) {
        double freq = std::clamp(freqAtX(event->pos().x()), f_min_hz_, f_max_hz_);
        if (drag_ == DragTarget::Low) {
            f_low_hz_ = std::min(freq, f_high_hz_);
        } else {
            f_high_hz_ = std::max(freq, f_low_hz_);
        }
        emit frequenciesChanged(f_low_hz_, f_high_hz_);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void SpectrumPlotWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        drag_ = DragTarget::None;
    }
    QWidget::mouseReleaseEvent(event);
}

}  // namespace kubik
