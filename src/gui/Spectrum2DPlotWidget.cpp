#include "Spectrum2DPlotWidget.hpp"
#include "kubik/AmplitudeClip.hpp"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace kubik {

namespace {

constexpr int kLeft = 56;
constexpr int kRight = 16;
constexpr int kTop = 22;
constexpr int kBottom = 40;

QString formatK(double k) {
    if (std::abs(k) >= 10.0) {
        return QString::number(k, 'f', 0);
    }
    if (std::abs(k) >= 1.0) {
        return QString::number(k, 'f', 2);
    }
    return QString::number(k, 'g', 3);
}

}  // namespace

Spectrum2DPlotWidget::Spectrum2DPlotWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(380);
}

void Spectrum2DPlotWidget::setSpectrum(const Spectrum2D& spec) {
    spec_ = spec;
    updateAmpClipRange();
    update();
}

void Spectrum2DPlotWidget::setClipPercent(float clip_percent) {
    clip_percent_ = clip_percent;
    updateAmpClipRange();
    update();
}

void Spectrum2DPlotWidget::updateAmpClipRange() {
    if (spec_.amps.empty()) {
        amp_vmin_ = 0.0;
        amp_vmax_ = 1.0;
        return;
    }
    std::vector<float> amps;
    amps.reserve(spec_.amps.size());
    for (double a : spec_.amps) {
        amps.push_back(static_cast<float>(a));
    }
    float vmin = 0.f;
    float vmax = 1.f;
    amplitudeClipRange(amps, clip_percent_, vmin, vmax);
    amp_vmin_ = static_cast<double>(std::max(0.f, vmin));
    amp_vmax_ = static_cast<double>(vmax);
    if (!(amp_vmax_ > amp_vmin_)) {
        amp_vmax_ = amp_vmin_ + 1.0;
    }
}

void Spectrum2DPlotWidget::setMaskOverlay(const std::vector<float>& mask, int w, int h) {
    mask_ = mask;
    mask_w_ = w;
    mask_h_ = h;
    update();
}

void Spectrum2DPlotWidget::setMaskOverlayEnabled(bool enabled) {
    mask_enabled_ = enabled;
    update();
}

QRectF Spectrum2DPlotWidget::plotRect() const {
    const double avail_w = std::max(1.0, static_cast<double>(width() - kLeft - kRight));
    const double avail_h = std::max(1.0, static_cast<double>(height() - kTop - kBottom));

    double k_span_xl = 1.0;
    double k_span_il = 1.0;
    if (spec_.k_xl.size() >= 2) {
        k_span_xl = std::max(1e-9, spec_.k_xl.back() - spec_.k_xl.front());
    }
    if (spec_.k_il.size() >= 2) {
        k_span_il = std::max(1e-9, spec_.k_il.back() - spec_.k_il.front());
    }

    double plot_w = avail_w;
    double plot_h = plot_w * k_span_il / k_span_xl;
    if (plot_h > avail_h) {
        plot_h = avail_h;
        plot_w = plot_h * k_span_xl / k_span_il;
    }

    const double offset_x = kLeft + (avail_w - plot_w) * 0.5;
    const double offset_y = kTop + (avail_h - plot_h) * 0.5;
    return QRectF(offset_x, offset_y, plot_w, plot_h);
}

void Spectrum2DPlotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().brush(QPalette::Window));

    const QRectF plot = plotRect();
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(plot);

    if (spec_.w > 0 && spec_.h > 0 && spec_.amps.size() == static_cast<std::size_t>(spec_.w * spec_.h)) {
        QImage img(static_cast<int>(plot.width()), static_cast<int>(plot.height()), QImage::Format_RGB32);
        for (int y = 0; y < img.height(); ++y) {
            const int sy =
                std::min(spec_.h - 1, y * spec_.h / std::max(1, img.height()));
            for (int x = 0; x < img.width(); ++x) {
                const int sx =
                    std::min(spec_.w - 1, x * spec_.w / std::max(1, img.width()));
                const double a =
                    spec_.amps[static_cast<std::size_t>(sy) * static_cast<std::size_t>(spec_.w) +
                               static_cast<std::size_t>(sx)];
                const double lo = std::log1p(amp_vmin_);
                const double hi = std::log1p(amp_vmax_);
                const double clipped = std::clamp(a, amp_vmin_, amp_vmax_);
                const double norm = (std::log1p(clipped) - lo) / std::max(hi - lo, 1e-12);
                const int g = static_cast<int>(std::clamp(norm, 0.0, 1.0) * 255.0);
                img.setPixel(x, y, qRgb(g, g, static_cast<int>(g * 0.85)));
            }
        }
        p.drawImage(plot.topLeft(), img);
    }
    if (mask_enabled_ && mask_w_ > 0 && mask_h_ > 0 && static_cast<int>(mask_.size()) == mask_w_ * mask_h_) {
        p.save();
        p.setOpacity(0.45);
        QImage mimg(static_cast<int>(plot.width()), static_cast<int>(plot.height()), QImage::Format_ARGB32);
        for (int y = 0; y < mimg.height(); ++y) {
            const int sy = std::min(mask_h_ - 1, y * mask_h_ / std::max(1, mimg.height()));
            for (int x = 0; x < mimg.width(); ++x) {
                const int sx = std::min(mask_w_ - 1, x * mask_w_ / std::max(1, mimg.width()));
                const float mv =
                    mask_[static_cast<std::size_t>(sy) * static_cast<std::size_t>(mask_w_) + static_cast<std::size_t>(sx)];
                const int r = static_cast<int>((1.0f - std::clamp(mv, 0.0f, 1.0f)) * 255.0f);
                mimg.setPixel(x, y, qRgba(r, 30, 30, 180));
            }
        }
        p.drawImage(plot.topLeft(), mimg);
        p.restore();
    }

    p.setPen(palette().color(QPalette::Text));
    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);

    if (!spec_.k_xl.empty()) {
        const int nticks = 5;
        for (int i = 0; i <= nticks; ++i) {
            const int idx = i * (static_cast<int>(spec_.k_xl.size()) - 1) / nticks;
            const int x = static_cast<int>(
                plot.left() + (static_cast<double>(idx) + 0.5) / static_cast<double>(spec_.k_xl.size()) *
                                  plot.width());
            p.drawLine(x, static_cast<int>(plot.bottom()), x, static_cast<int>(plot.bottom()) + 4);
            const QString label = formatK(spec_.k_xl[static_cast<std::size_t>(idx)]);
            p.drawText(QRect(x - 30, static_cast<int>(plot.bottom()) + 4, 60, 14), Qt::AlignHCenter | Qt::AlignTop,
                       label);
        }
    }

    if (!spec_.k_il.empty()) {
        const int nticks = 5;
        for (int i = 0; i <= nticks; ++i) {
            const int idx = i * (static_cast<int>(spec_.k_il.size()) - 1) / nticks;
            const int y = static_cast<int>(
                plot.top() + (static_cast<double>(idx) + 0.5) / static_cast<double>(spec_.k_il.size()) *
                                 plot.height());
            p.drawLine(static_cast<int>(plot.left()) - 4, y, static_cast<int>(plot.left()), y);
            const QString label = formatK(spec_.k_il[static_cast<std::size_t>(idx)]);
            p.drawText(QRect(0, y - 7, kLeft - 6, 14), Qt::AlignRight | Qt::AlignVCenter, label);
        }
    }

    QFont title = f;
    title.setBold(true);
    p.setFont(title);
    p.drawText(QRectF(plot.left(), height() - 18, plot.width(), 16), Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("k XL"));
    p.save();
    p.translate(12, plot.top() + plot.height() / 2.0);
    p.rotate(-90.0);
    p.drawText(QRect(-50, -8, 100, 16), Qt::AlignCenter, QStringLiteral("k IL"));
    p.restore();
}

}  // namespace kubik
