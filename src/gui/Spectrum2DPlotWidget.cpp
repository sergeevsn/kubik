#include "Spectrum2DPlotWidget.hpp"
#include "kubik/AmplitudeClip.hpp"

#include <QMouseEvent>
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
constexpr int kHitPx = 8;

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
    setMouseTracking(true);
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

void Spectrum2DPlotWidget::setFilterParams(const FftFilter2DParams& params) {
    params_ = params;
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

double Spectrum2DPlotWidget::kMaxXl() const {
    if (spec_.k_xl.empty()) {
        return 0.5;
    }
    return std::max(std::abs(spec_.k_xl.front()), std::abs(spec_.k_xl.back()));
}

double Spectrum2DPlotWidget::kMaxIl() const {
    if (spec_.k_il.empty()) {
        return 0.5;
    }
    return std::max(std::abs(spec_.k_il.front()), std::abs(spec_.k_il.back()));
}

double Spectrum2DPlotWidget::kXlAtX(int px) const {
    const QRectF plot = plotRect();
    if (spec_.k_xl.empty() || plot.width() <= 1.0) {
        return 0.0;
    }
    const double rel = (static_cast<double>(px) - plot.left()) / plot.width();
    const int idx = std::clamp(static_cast<int>(rel * spec_.k_xl.size()), 0,
                               static_cast<int>(spec_.k_xl.size()) - 1);
    return spec_.k_xl[static_cast<std::size_t>(idx)];
}

double Spectrum2DPlotWidget::kIlAtY(int py) const {
    const QRectF plot = plotRect();
    if (spec_.k_il.empty() || plot.height() <= 1.0) {
        return 0.0;
    }
    const double rel = (static_cast<double>(py) - plot.top()) / plot.height();
    const int idx = std::clamp(static_cast<int>(rel * spec_.k_il.size()), 0,
                               static_cast<int>(spec_.k_il.size()) - 1);
    return spec_.k_il[static_cast<std::size_t>(idx)];
}

int Spectrum2DPlotWidget::xAtKXl(double k) const {
    const QRectF plot = plotRect();
    if (spec_.k_xl.empty()) {
        return static_cast<int>(plot.left());
    }
    int best = 0;
    double best_d = std::abs(spec_.k_xl[0] - k);
    for (int i = 1; i < static_cast<int>(spec_.k_xl.size()); ++i) {
        const double d = std::abs(spec_.k_xl[static_cast<std::size_t>(i)] - k);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return static_cast<int>(plot.left() +
                            (static_cast<double>(best) + 0.5) / static_cast<double>(spec_.k_xl.size()) *
                                plot.width());
}

int Spectrum2DPlotWidget::yAtKIl(double k) const {
    const QRectF plot = plotRect();
    if (spec_.k_il.empty()) {
        return static_cast<int>(plot.top());
    }
    int best = 0;
    double best_d = std::abs(spec_.k_il[0] - k);
    for (int i = 1; i < static_cast<int>(spec_.k_il.size()); ++i) {
        const double d = std::abs(spec_.k_il[static_cast<std::size_t>(i)] - k);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return static_cast<int>(plot.top() +
                            (static_cast<double>(best) + 0.5) / static_cast<double>(spec_.k_il.size()) *
                                plot.height());
}

Spectrum2DPlotWidget::DragTarget Spectrum2DPlotWidget::hitTest(int px, int py) const {
    const QRectF plot = plotRect();
    if (!plot.contains(px, py)) {
        return DragTarget::None;
    }

    const bool show_il = params_.type == FftFilter2DType::FootprintIl ||
                         params_.type == FftFilter2DType::FootprintIlXl;
    const bool show_xl = params_.type == FftFilter2DType::FootprintXl ||
                         params_.type == FftFilter2DType::FootprintIlXl;

    DragTarget best = DragTarget::None;
    int best_dist = kHitPx + 1;

    auto consider_h = [&](double k, DragTarget target) {
        const int d_pos = std::abs(py - yAtKIl(k));
        const int d_neg = std::abs(py - yAtKIl(-k));
        const int d = std::min(d_pos, d_neg);
        if (d <= kHitPx && d < best_dist) {
            best_dist = d;
            best = target;
        }
    };

    auto consider_v = [&](double k, DragTarget target) {
        const int d_pos = std::abs(px - xAtKXl(k));
        const int d_neg = std::abs(px - xAtKXl(-k));
        const int d = std::min(d_pos, d_neg);
        if (d <= kHitPx && d < best_dist) {
            best_dist = d;
            best = target;
        }
    };

    if (show_il) {
        consider_h(params_.k_pass, DragTarget::PassIl);
        consider_h(params_.k_cut_il, DragTarget::CutIl);
    }
    if (show_xl) {
        consider_v(params_.k_pass, DragTarget::PassXl);
        consider_v(params_.k_cut_xl, DragTarget::CutXl);
    }

    return best;
}

void Spectrum2DPlotWidget::applyDrag(int px, int py) {
    FftFilter2DParams p = params_;

    switch (drag_) {
    case DragTarget::PassIl: {
        const double k = std::clamp(std::abs(kIlAtY(py)), 0.0, kMaxIl());
        p.k_pass = k;
        if (p.k_pass > p.k_cut_il) {
            p.k_pass = p.k_cut_il;
        }
        if (p.k_pass > p.k_cut_xl) {
            p.k_pass = std::min(p.k_pass, p.k_cut_xl);
        }
        break;
    }
    case DragTarget::CutIl: {
        const double k = std::clamp(std::abs(kIlAtY(py)), p.k_pass, kMaxIl());
        p.k_cut_il = k;
        break;
    }
    case DragTarget::PassXl: {
        const double k = std::clamp(std::abs(kXlAtX(px)), 0.0, kMaxXl());
        p.k_pass = k;
        if (p.k_pass > p.k_cut_il) {
            p.k_pass = std::min(p.k_pass, p.k_cut_il);
        }
        if (p.k_pass > p.k_cut_xl) {
            p.k_pass = std::min(p.k_pass, p.k_cut_xl);
        }
        break;
    }
    case DragTarget::CutXl: {
        const double k = std::clamp(std::abs(kXlAtX(px)), p.k_pass, kMaxXl());
        p.k_cut_xl = k;
        break;
    }
    case DragTarget::None:
        return;
    }

    const double min_cut = std::min(p.k_cut_il, p.k_cut_xl);
    const double max_smooth = std::max(0.0, (min_cut - p.k_pass) * 0.5);
    if (p.k_smooth > max_smooth) {
        p.k_smooth = max_smooth;
    }

    params_ = p;
    emit filterParamsChanged(p);
    update();
}

void Spectrum2DPlotWidget::updateHoverCursor(int px, int py) {
    const DragTarget hit = hitTest(px, py);
    switch (hit) {
    case DragTarget::PassIl:
    case DragTarget::CutIl:
        setCursor(Qt::SizeVerCursor);
        break;
    case DragTarget::PassXl:
    case DragTarget::CutXl:
        setCursor(Qt::SizeHorCursor);
        break;
    case DragTarget::None:
        unsetCursor();
        break;
    }
}

void Spectrum2DPlotWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        drag_ = hitTest(event->pos().x(), event->pos().y());
    }
    QWidget::mousePressEvent(event);
}

void Spectrum2DPlotWidget::mouseMoveEvent(QMouseEvent* event) {
    if (drag_ != DragTarget::None) {
        applyDrag(event->pos().x(), event->pos().y());
    } else {
        updateHoverCursor(event->pos().x(), event->pos().y());
    }
    QWidget::mouseMoveEvent(event);
}

void Spectrum2DPlotWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        drag_ = DragTarget::None;
        updateHoverCursor(event->pos().x(), event->pos().y());
    }
    QWidget::mouseReleaseEvent(event);
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

    auto drawBandAlongIl = [&](double k_limit, const QColor& color, Qt::PenStyle style) {
        const int y0 = yAtKIl(-k_limit);
        const int y1 = yAtKIl(k_limit);
        QPen pen(color, 1.2, style);
        p.setPen(pen);
        p.drawLine(static_cast<int>(plot.left()), y0, static_cast<int>(plot.right()), y0);
        p.drawLine(static_cast<int>(plot.left()), y1, static_cast<int>(plot.right()), y1);
    };

    auto drawBandAlongXl = [&](double k_limit, const QColor& color, Qt::PenStyle style) {
        const int x0 = xAtKXl(-k_limit);
        const int x1 = xAtKXl(k_limit);
        QPen pen(color, 1.2, style);
        p.setPen(pen);
        p.drawLine(x0, static_cast<int>(plot.top()), x0, static_cast<int>(plot.bottom()));
        p.drawLine(x1, static_cast<int>(plot.top()), x1, static_cast<int>(plot.bottom()));
    };

    const bool show_il = params_.type == FftFilter2DType::FootprintIl ||
                         params_.type == FftFilter2DType::FootprintIlXl;
    const bool show_xl = params_.type == FftFilter2DType::FootprintXl ||
                         params_.type == FftFilter2DType::FootprintIlXl;

    if (show_il) {
        drawBandAlongIl(params_.k_pass, QColor(80, 220, 140), Qt::SolidLine);
        drawBandAlongIl(params_.k_cut_il, QColor(255, 120, 80), Qt::DashLine);
        if (params_.k_smooth > 0.0) {
            drawBandAlongIl(params_.k_pass + params_.k_smooth, QColor(120, 180, 255), Qt::DotLine);
            drawBandAlongIl(params_.k_cut_il - params_.k_smooth, QColor(120, 180, 255), Qt::DotLine);
        }
    }
    if (show_xl) {
        drawBandAlongXl(params_.k_pass, QColor(80, 220, 140), Qt::SolidLine);
        drawBandAlongXl(params_.k_cut_xl, QColor(255, 120, 80), Qt::DashLine);
        if (params_.k_smooth > 0.0) {
            drawBandAlongXl(params_.k_pass + params_.k_smooth, QColor(120, 180, 255), Qt::DotLine);
            drawBandAlongXl(params_.k_cut_xl - params_.k_smooth, QColor(120, 180, 255), Qt::DotLine);
        }
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
