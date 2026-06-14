#include "SurveyMapWidget.hpp"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace kubik {

namespace {

constexpr int kMarginLeft = 52;
constexpr int kMarginRight = 16;
constexpr int kMarginTop = 22;
constexpr int kMarginBottom = 36;
constexpr int kMaxPlotWidth = 640;
constexpr int kMaxPlotHeight = 480;
constexpr int kMinPlotWidth = 180;
constexpr int kMinPlotHeight = 140;

QSize plotSizeForBounds(const QRectF& bounds, int max_plot_w, int max_plot_h) {
    const double span_x = std::max(bounds.width(), 1e-9);
    const double span_y = std::max(bounds.height(), 1e-9);

    int plot_w = max_plot_w;
    int plot_h = static_cast<int>(std::lround(plot_w * span_y / span_x));
    if (plot_h > max_plot_h) {
        plot_h = max_plot_h;
        plot_w = static_cast<int>(std::lround(plot_h * span_x / span_y));
    }
    plot_w = std::clamp(plot_w, kMinPlotWidth, kMaxPlotWidth);
    plot_h = std::clamp(plot_h, kMinPlotHeight, kMaxPlotHeight);
    return QSize(plot_w, plot_h);
}

}  // namespace

SurveyMapWidget::SurveyMapWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setMaximumSize(kMarginLeft + kMarginRight + kMaxPlotWidth,
                   kMarginTop + kMarginBottom + kMaxPlotHeight);
}

void SurveyMapWidget::setSurveyData(const SurveyCoordinateStats& stats) {
    points_ = stats.cdp_points;
    corners_ = stats.corners;
    updateGeometry();
    update();
}

QSize SurveyMapWidget::sizeHint() const {
    const QSize plot = plotSizeForBounds(dataBounds(), kMaxPlotWidth, kMaxPlotHeight);
    return QSize(plot.width() + kMarginLeft + kMarginRight, plot.height() + kMarginTop + kMarginBottom);
}

int SurveyMapWidget::heightForWidth(int width) const {
    if (width <= 0) {
        return sizeHint().height();
    }
    const int plot_w = std::clamp(width - kMarginLeft - kMarginRight, kMinPlotWidth, kMaxPlotWidth);
    const QRectF bounds = dataBounds();
    const double span_x = std::max(bounds.width(), 1e-9);
    const double span_y = std::max(bounds.height(), 1e-9);
    int plot_h = static_cast<int>(std::lround(plot_w * span_y / span_x));
    plot_h = std::clamp(plot_h, kMinPlotHeight, kMaxPlotHeight);
    return plot_h + kMarginTop + kMarginBottom;
}

QRectF SurveyMapWidget::plotArea() const {
    return QRectF(kMarginLeft, kMarginTop,
                  std::max(1.0, static_cast<double>(width() - kMarginLeft - kMarginRight)),
                  std::max(1.0, static_cast<double>(height() - kMarginTop - kMarginBottom)));
}

QRectF SurveyMapWidget::dataBounds() const {
    QRectF bounds;
    bool has_point = false;

    auto extend = [&](double x, double y) {
        if (!std::isfinite(x) || !std::isfinite(y)) {
            return;
        }
        if (!has_point) {
            bounds = QRectF(x, y, 0.0, 0.0);
            has_point = true;
        } else {
            const double x0 = std::min(bounds.left(), x);
            const double y0 = std::min(bounds.top(), y);
            const double x1 = std::max(bounds.right(), x);
            const double y1 = std::max(bounds.bottom(), y);
            bounds = QRectF(x0, y0, x1 - x0, y1 - y0);
        }
    };

    for (const SurveyCdpPoint& pt : points_) {
        extend(pt.cdp_x, pt.cdp_y);
    }
    for (const SurveyCornerPoint& pt : corners_) {
        extend(pt.cdp_x, pt.cdp_y);
    }

    if (!has_point) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    const double pad_x = std::max(bounds.width() * 0.05, 1.0);
    const double pad_y = std::max(bounds.height() * 0.05, 1.0);
    return bounds.adjusted(-pad_x, -pad_y, pad_x, pad_y);
}

SurveyMapWidget::PlotLayout SurveyMapWidget::computePlotLayout(const QRectF& bounds) const {
    PlotLayout layout;
    layout.area = plotArea();

    const double span_x = std::max(bounds.width(), 1e-9);
    const double span_y = std::max(bounds.height(), 1e-9);
    const double scale = std::min(layout.area.width() / span_x, layout.area.height() / span_y);
    const double plot_w = span_x * scale;
    const double plot_h = span_y * scale;
    const double left = layout.area.left() + (layout.area.width() - plot_w) * 0.5;
    const double top = layout.area.top() + (layout.area.height() - plot_h) * 0.5;
    layout.plot = QRectF(left, top, plot_w, plot_h);
    return layout;
}

QPointF SurveyMapWidget::mapToScreen(double x, double y, const PlotLayout& layout,
                                     const QRectF& bounds) const {
    const double span_x = std::max(bounds.width(), 1e-9);
    const double span_y = std::max(bounds.height(), 1e-9);
    const double rel_x = (x - bounds.left()) / span_x;
    const double rel_y = (y - bounds.top()) / span_y;
    return QPointF(layout.plot.left() + rel_x * layout.plot.width(),
                   layout.plot.bottom() - rel_y * layout.plot.height());
}

void SurveyMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().brush(QPalette::Window));

    const QRectF bounds = dataBounds();
    if (points_.empty() && corners_.empty()) {
        p.setPen(palette().color(QPalette::Text));
        p.drawText(rect(), Qt::AlignCenter, tr("Нет координат CDP"));
        return;
    }

    const PlotLayout layout = computePlotLayout(bounds);
    if (layout.plot.width() < 20.0 || layout.plot.height() < 20.0) {
        return;
    }

    p.setRenderHint(QPainter::Antialiasing, true);

    p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(layout.plot);

    p.setPen(QPen(QColor(120, 170, 220, 180), 1.0));
    p.setBrush(QColor(120, 170, 220, 90));
    for (const SurveyCdpPoint& pt : points_) {
        const QPointF sp = mapToScreen(pt.cdp_x, pt.cdp_y, layout, bounds);
        p.drawEllipse(sp, 1.2, 1.2);
    }

    if (corners_.size() >= 4) {
        QPolygonF rect_poly;
        for (const SurveyCornerPoint& corner : corners_) {
            rect_poly << mapToScreen(corner.cdp_x, corner.cdp_y, layout, bounds);
        }
        rect_poly << rect_poly.first();

        QPen rect_pen(QColor(255, 200, 80), 1.6, Qt::DashLine);
        rect_pen.setCosmetic(true);
        p.setPen(rect_pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(rect_poly);
    }

    QFont corner_font = p.font();
    corner_font.setBold(true);
    corner_font.setPointSizeF(std::max(8.0, corner_font.pointSizeF()));
    p.setFont(corner_font);

    for (const SurveyCornerPoint& corner : corners_) {
        const QPointF sp = mapToScreen(corner.cdp_x, corner.cdp_y, layout, bounds);

        p.setPen(QPen(QColor(30, 30, 30), 2.0));
        p.setBrush(QColor(255, 140, 60));
        p.drawEllipse(sp, 7.0, 7.0);

        p.setPen(QPen(Qt::white, 1.0));
        p.drawText(QRectF(sp.x() - 12.0, sp.y() - 12.0, 24.0, 24.0), Qt::AlignCenter,
                   QString::number(corner.point_num));
    }

    p.setPen(palette().color(QPalette::Text));
    QFont axis_font = p.font();
    axis_font.setBold(false);
    axis_font.setPointSize(8);
    p.setFont(axis_font);
    p.drawText(QRectF(layout.plot.left(), height() - 28.0, layout.plot.width(), 18.0),
               Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("CDP_X →"));
    p.save();
    p.translate(14.0, layout.plot.top() + layout.plot.height() / 2.0);
    p.rotate(-90);
    p.drawText(QRect(-50, -8, 100, 16), Qt::AlignCenter, QStringLiteral("CDP_Y →"));
    p.restore();
}

}  // namespace kubik
