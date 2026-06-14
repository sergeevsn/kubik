#include "CubeNavigator.hpp"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace kubik {

CubeNavigator::CubeNavigator(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(140);
    setMinimumHeight(200);
}

void CubeNavigator::setCubeSize(int n_il, int n_xl, int n_t) {
    n_il_ = std::max(1, n_il);
    n_xl_ = std::max(1, n_xl);
    n_t_ = std::max(1, n_t);
    il_idx_ = std::clamp(il_idx_, 0, n_il_ - 1);
    xl_idx_ = std::clamp(xl_idx_, 0, n_xl_ - 1);
    t_idx_ = std::clamp(t_idx_, 0, n_t_ - 1);
    update();
}

void CubeNavigator::setSlicePositions(int il_idx, int xl_idx, int t_idx) {
    il_idx_ = il_idx;
    xl_idx_ = xl_idx;
    t_idx_ = t_idx;
    update();
}

void CubeNavigator::setSliceMode(SliceMode mode) {
    mode_ = mode;
    update();
}

QRectF CubeNavigator::cubeRect() const {
    const qreal m_lr = 24.0;
    const qreal m_bottom = 22.0;
    const qreal m_top = 38.0;  // запас под уход задней грани вверх по оси IL
    const qreal w = width() - 2 * m_lr;
    const qreal h = height() - m_top - m_bottom;
    const qreal side = std::min(w, h);
    const qreal x = m_lr + (w - side) / 2.0;
    const qreal y = m_top + std::max(0.0, (h - side) / 2.0);
    return QRectF(x, y, side, side);
}

void CubeNavigator::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().brush(QPalette::Window));

    const QRectF base = cubeRect();
    if (base.width() < 20) {
        return;
    }

    // Передняя грань: горизонталь = XL, вертикаль = T (время).
    // В глубину (вверх-вправо) = IL.
    const QPointF a = base.topLeft();
    const QPointF b = base.topRight();
    const QPointF c = base.bottomRight();
    const QPointF d = base.bottomLeft();
    const QPointF shift(base.width() * 0.32, -base.height() * 0.26);
    const QPointF a2 = a + shift;
    const QPointF b2 = b + shift;
    const QPointF c2 = c + shift;
    const QPointF d2 = d + shift;

    auto lerp = [](const QPointF& p0, const QPointF& p1, double t) {
        return QPointF(p0.x() + (p1.x() - p0.x()) * t, p0.y() + (p1.y() - p0.y()) * t);
    };

    const double fu = n_il_ > 1 ? static_cast<double>(il_idx_) / (n_il_ - 1) : 0.0;
    const double fv = n_xl_ > 1 ? static_cast<double>(xl_idx_) / (n_xl_ - 1) : 0.0;
    const double fw = n_t_ > 1 ? static_cast<double>(t_idx_) / (n_t_ - 1) : 0.0;

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(90, 90, 100), 1.2));
    p.setBrush(QColor(60, 60, 70, 40));

    QPolygonF back;
    back << a2 << b2 << c2 << d2;
    p.drawPolygon(back);

    p.drawLine(a, a2);
    p.drawLine(b, b2);
    p.drawLine(c, c2);
    p.drawLine(d, d2);
    p.drawLine(a, b);
    p.drawLine(b, c);
    p.drawLine(c, d);
    p.drawLine(d, a);
    p.drawLine(a2, b2);
    p.drawLine(b2, c2);
    p.drawLine(c2, d2);
    p.drawLine(d2, a2);
    p.drawLine(b, b2);
    p.drawLine(c, c2);

    auto drawSlicePlane = [&](const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3,
                              const QColor& col, qreal pen_w) {
        p.setPen(QPen(col, pen_w));
        p.drawLine(p0, p1);
        p.drawLine(p1, p2);
        p.drawLine(p2, p3);
        p.drawLine(p3, p0);
    };

    switch (mode_) {
    case SliceMode::Inline:
        // Плоскость ⊥ IL (параллельна передней грани XL×T).
        drawSlicePlane(lerp(a, a2, fu), lerp(b, b2, fu), lerp(c, c2, fu), lerp(d, d2, fu),
                       QColor(255, 120, 80), 2.0);
        break;
    case SliceMode::Crossline:
        // Плоскость ⊥ XL (вертикальная на фронте, T вверх-вниз).
        drawSlicePlane(lerp(a, b, fv), lerp(a2, b2, fv), lerp(d2, c2, fv), lerp(d, c, fv),
                       QColor(80, 180, 255), 2.0);
        break;
    case SliceMode::Time:
        // Плоскость ⊥ T (горизонтальная, фиксированное время).
        drawSlicePlane(lerp(a, d, fw), lerp(b, c, fw), lerp(b2, c2, fw), lerp(a2, d2, fw),
                       QColor(120, 255, 120), 2.0);
        break;
    }

    p.setPen(palette().color(QPalette::Text));
    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    p.drawText(QRectF(base.left(), base.top() - 16, base.width(), 14), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("XL →"));
    p.save();
    p.translate(base.left() - 14, base.top() + base.height() / 2);
    p.rotate(-90);
    p.drawText(QRect(-40, -6, 80, 12), Qt::AlignCenter, QStringLiteral("T →"));
    p.restore();
    p.drawText(QRectF(0, height() - 20, width(), 18), Qt::AlignCenter,
               tr("IL %1 | XL %2 | T %3").arg(il_idx_).arg(xl_idx_).arg(t_idx_));
}

void CubeNavigator::mapClick(const QPoint& pos) {
    const QRectF base = cubeRect();
    if (!base.contains(pos)) {
        return;
    }
    const double fx = (pos.x() - base.left()) / base.width();
    const double fy = (pos.y() - base.top()) / base.height();

    switch (mode_) {
    case SliceMode::Inline: {
        const int il = std::clamp(static_cast<int>(fx * (n_il_ - 1) + 0.5), 0, n_il_ - 1);
        emit inlineJumpRequested(il);
        break;
    }
    case SliceMode::Crossline: {
        const int xl = std::clamp(static_cast<int>(fx * (n_xl_ - 1) + 0.5), 0, n_xl_ - 1);
        emit crosslineJumpRequested(xl);
        break;
    }
    case SliceMode::Time: {
        const int t = std::clamp(static_cast<int>(fy * (n_t_ - 1) + 0.5), 0, n_t_ - 1);
        emit timeJumpRequested(t);
        break;
    }
    }
}

void CubeNavigator::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        mapClick(event->pos());
    }
    QWidget::mousePressEvent(event);
}

}  // namespace kubik
