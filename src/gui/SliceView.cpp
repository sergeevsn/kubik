#include "SliceView.hpp"
#include "ColorSchemes.hpp"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kLeftMargin = 56;
constexpr int kRightMargin = 12;
constexpr int kTopMargin = 36;
constexpr int kBottomMargin = 12;

QString horizAxisLabel(kubik::SliceMode mode) {
    switch (mode) {
    case kubik::SliceMode::Inline:
        return QStringLiteral("Crossline");
    case kubik::SliceMode::Crossline:
        return QStringLiteral("Inline");
    case kubik::SliceMode::Time:
        return QStringLiteral("Crossline");
    }
    return {};
}

QString vertAxisLabel(kubik::SliceMode mode) {
    switch (mode) {
    case kubik::SliceMode::Inline:
    case kubik::SliceMode::Crossline:
        return QStringLiteral("Time, ms");
    case kubik::SliceMode::Time:
        return QStringLiteral("Inline");
    }
    return {};
}

bool vertAxisIsTimeMs(kubik::SliceMode mode) {
    return mode == kubik::SliceMode::Inline || mode == kubik::SliceMode::Crossline;
}

int chooseIndexTickStep(int count, int max_ticks = 6) {
    if (count <= 1) {
        return 1;
    }
    const int raw = std::max(1, (count + max_ticks - 1) / max_ticks);
    const int candidates[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000};
    for (int c : candidates) {
        if (c >= raw) {
            return c;
        }
    }
    return raw;
}

int chooseTimeTickStepMs(double total_ms, int max_ticks = 6) {
    if (total_ms <= 0.0) {
        return 100;
    }
    const double raw = total_ms / static_cast<double>(max_ticks);
    const int candidates[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000};
    for (int c : candidates) {
        if (static_cast<double>(c) >= raw) {
            return c;
        }
    }
    return static_cast<int>(raw);
}

void drawTopTicks(QPainter& p,
                  int plot_left,
                  int plot_right,
                  int axis_y,
                  int plot_w,
                  const std::vector<int32_t>& labels,
                  const QFont& font) {
    const int count = static_cast<int>(labels.size());
    if (count <= 0 || plot_w <= 0) {
        return;
    }
    p.setFont(font);
    const int step = chooseIndexTickStep(count, 6);
    const int last_idx = count - 1;

    for (int idx = 0; idx <= last_idx; idx += step) {
        const double rel = last_idx > 0 ? static_cast<double>(idx) / static_cast<double>(last_idx) : 0.0;
        const int x = plot_left + static_cast<int>(rel * static_cast<double>(plot_w - 1) + 0.5);
        p.drawLine(x, axis_y, x, axis_y - 4);
        const QString label = QString::number(labels[static_cast<std::size_t>(idx)]);
        const QRect text_rect(x - 28, axis_y - 18, 56, 14);
        p.drawText(text_rect, Qt::AlignHCenter | Qt::AlignBottom, label);
    }
    if (last_idx % step != 0) {
        const int x = plot_right - 1;
        p.drawLine(x, axis_y, x, axis_y - 4);
        const QString label = QString::number(labels[static_cast<std::size_t>(last_idx)]);
        const QRect text_rect(x - 28, axis_y - 18, 56, 14);
        p.drawText(text_rect, Qt::AlignHCenter | Qt::AlignBottom, label);
    }
}

void drawLeftTimeTicks(QPainter& p,
                       int axis_x,
                       int plot_top,
                       int plot_bottom,
                       int plot_h,
                       int sample_count,
                       float dt_ms,
                       const QFont& font) {
    if (sample_count <= 0 || plot_h <= 0 || dt_ms <= 0.f) {
        return;
    }
    p.setFont(font);
    const double total_ms = static_cast<double>(sample_count - 1) * static_cast<double>(dt_ms);
    const int step_ms = chooseTimeTickStepMs(total_ms, 6);
    if (step_ms <= 0) {
        return;
    }

    const int first_ms = 0;
    const int last_ms = static_cast<int>(total_ms + 0.5);

    for (int t_ms = first_ms; t_ms <= last_ms; t_ms += step_ms) {
        const double rel = total_ms > 0.0 ? static_cast<double>(t_ms) / total_ms : 0.0;
        if (rel < 0.0 || rel > 1.0) {
            continue;
        }
        const int y = plot_top + static_cast<int>(rel * static_cast<double>(plot_h - 1) + 0.5);
        p.drawLine(axis_x, y, axis_x - 4, y);
        const QString label = QString::number(t_ms);
        const QRect text_rect(0, y - 7, axis_x - 6, 14);
        p.drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter, label);
    }
    if (last_ms % step_ms != 0) {
        const int y = plot_bottom - 1;
        p.drawLine(axis_x, y, axis_x - 4, y);
        const QString label = QString::number(last_ms);
        const QRect text_rect(0, y - 7, axis_x - 6, 14);
        p.drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter, label);
    }
}

int indexTickY(int idx, int last_idx, int plot_top, int plot_bottom, int plot_h, bool origin_at_bottom) {
    const double rel = last_idx > 0 ? static_cast<double>(idx) / static_cast<double>(last_idx) : 0.0;
    if (origin_at_bottom) {
        return plot_bottom - 1 - static_cast<int>(rel * static_cast<double>(plot_h - 1) + 0.5);
    }
    return plot_top + static_cast<int>(rel * static_cast<double>(plot_h - 1) + 0.5);
}

void drawLeftIndexTicks(QPainter& p,
                        int axis_x,
                        int plot_top,
                        int plot_bottom,
                        int plot_h,
                        const std::vector<int32_t>& labels,
                        const QFont& font,
                        bool origin_at_bottom) {
    const int count = static_cast<int>(labels.size());
    if (count <= 0 || plot_h <= 0) {
        return;
    }
    p.setFont(font);
    const int step = chooseIndexTickStep(count, 6);
    const int last_idx = count - 1;

    for (int idx = 0; idx <= last_idx; idx += step) {
        const int y = indexTickY(idx, last_idx, plot_top, plot_bottom, plot_h, origin_at_bottom);
        p.drawLine(axis_x, y, axis_x - 4, y);
        const QString label = QString::number(labels[static_cast<std::size_t>(idx)]);
        const QRect text_rect(0, y - 7, axis_x - 6, 14);
        p.drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter, label);
    }
    if (last_idx % step != 0) {
        const int y = indexTickY(last_idx, last_idx, plot_top, plot_bottom, plot_h, origin_at_bottom);
        p.drawLine(axis_x, y, axis_x - 4, y);
        const QString label = QString::number(labels[static_cast<std::size_t>(last_idx)]);
        const QRect text_rect(0, y - 7, axis_x - 6, 14);
        p.drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter, label);
    }
}

}  // namespace

namespace kubik {

SliceView::SliceView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    updateColorLut();
}

void SliceView::setSlice(const std::vector<float>& data, int width, int height, float vmin, float vmax) {
    data_ = data;
    width_ = width;
    height_ = height;
    vmin_ = vmin;
    vmax_ = vmax;
    if (!(vmax_ > vmin_)) {
        vmax_ = vmin_ + 1.f;
    }
    update();
}

void SliceView::setSliceMode(SliceMode mode) {
    mode_ = mode;
    update();
}

void SliceView::setAxisLabels(std::vector<int32_t> horiz_labels,
                              std::vector<int32_t> vert_index_labels,
                              bool vert_is_time,
                              float vert_step_ms) {
    horiz_labels_ = std::move(horiz_labels);
    vert_index_labels_ = std::move(vert_index_labels);
    vert_is_time_ = vert_is_time;
    vert_step_ms_ = vert_step_ms;
    update();
}

void SliceView::setColorMap(ColorMap cmap) {
    color_map_ = cmap;
    updateColorLut();
    update();
}

void SliceView::setSelectionMode(bool enabled) {
    selection_mode_ = enabled;
    selecting_ = false;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void SliceView::setCropRanges(int h_min, int h_max, int v_min, int v_max, bool mask_entire) {
    crop_h_min_ = h_min;
    crop_h_max_ = h_max;
    crop_v_min_ = v_min;
    crop_v_max_ = v_max;
    crop_mask_entire_ = mask_entire;
    update();
}

void SliceView::updateColorLut() {
    constexpr int kLutSize = 1024;
    color_lut_.resize(kLutSize);

    QString scheme;
    switch (color_map_) {
    case ColorMap::Grayscale:
        scheme = QStringLiteral("gray");
        break;
    case ColorMap::Viridis:
        scheme = QStringLiteral("viridis");
        break;
    case ColorMap::RedBlue:
        scheme = QStringLiteral("red_blue");
        break;
    case ColorMap::PetrelClassic:
        scheme = QStringLiteral("petrel_classic");
        break;
    case ColorMap::Kingdom:
        scheme = QStringLiteral("kingdom");
        break;
    }

    for (int i = 0; i < kLutSize; ++i) {
        const float norm = static_cast<float>(i) / static_cast<float>(kLutSize - 1);
        color_lut_[static_cast<std::size_t>(i)] = ColorSchemes::getColor(norm, scheme).rgb();
    }
}

QRect SliceView::plotPixelRect() const {
    const int plot_left = kLeftMargin;
    const int plot_right = std::max(plot_left + 1, width() - kRightMargin);
    const int plot_top = kTopMargin;
    const int plot_bottom = std::max(plot_top + 1, height() - kBottomMargin);
    return QRect(plot_left, plot_top, plot_right - plot_left, plot_bottom - plot_top);
}

void SliceView::indicesFromSelection(const QRect& sel_px, int& h0, int& h1, int& v0, int& v1) const {
    int ha = 0;
    int va = 0;
    int hb = 0;
    int vb = 0;
    mapPixelToIndices(sel_px.left(), sel_px.top(), ha, va);
    mapPixelToIndices(std::max(sel_px.left(), sel_px.right() - 1),
                      std::max(sel_px.top(), sel_px.bottom() - 1), hb, vb);
    h0 = std::min(ha, hb);
    h1 = std::max(ha, hb);
    v0 = std::min(va, vb);
    v1 = std::max(va, vb);
}

bool SliceView::mapPixelToIndices(int px, int py, int& out_h, int& out_v) const {
    if (width_ <= 0 || height_ <= 0 || data_.empty()) {
        return false;
    }
    const int plot_left = kLeftMargin;
    const int plot_right = std::max(plot_left + 1, width() - kRightMargin);
    const int plot_top = kTopMargin;
    const int plot_bottom = std::max(plot_top + 1, height() - kBottomMargin);
    if (px < plot_left || px >= plot_right || py < plot_top || py >= plot_bottom) {
        return false;
    }
    const int plot_w = plot_right - plot_left;
    const int plot_h = plot_bottom - plot_top;
    const int ix = static_cast<int>(
        static_cast<double>(px - plot_left) / static_cast<double>(plot_w) * width_);
    int iy = static_cast<int>(
        static_cast<double>(py - plot_top) / static_cast<double>(plot_h) * height_);
    if (mode_ == SliceMode::Time) {
        iy = height_ - 1 - iy;
    }
    if (ix < 0 || iy < 0 || ix >= width_ || iy >= height_) {
        return false;
    }
    out_h = ix;
    out_v = iy;
    return true;
}

void SliceView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), palette().brush(QPalette::Window));

    const int plot_left = kLeftMargin;
    const int plot_right = std::max(plot_left + 1, width() - kRightMargin);
    const int plot_top = kTopMargin;
    const int plot_bottom = std::max(plot_top + 1, height() - kBottomMargin);
    const int plot_w = plot_right - plot_left;
    const int plot_h = plot_bottom - plot_top;

    if (width_ <= 0 || height_ <= 0 || data_.empty() || plot_w <= 0 || plot_h <= 0) {
        p.setPen(palette().color(QPalette::Text));
        p.drawText(rect(), Qt::AlignCenter, tr("Откройте SEG-Y файл"));
        return;
    }

    QImage img(plot_w, plot_h, QImage::Format_RGB32);
    // Нейтральный фон (нулевая амплитуда ≈ 0.5 в LUT), чтобы сейсмика была видна на тёмной теме.
    img.fill(QColor(96, 96, 100).rgb());

    const bool flip_il_vert = mode_ == SliceMode::Time;
    for (int iy = 0; iy < plot_h; ++iy) {
        int src_y = std::min(
            height_ - 1,
            static_cast<int>(static_cast<double>(iy) / static_cast<double>(plot_h) * height_));
        if (flip_il_vert) {
            src_y = height_ - 1 - src_y;
        }
        for (int ix = 0; ix < plot_w; ++ix) {
            const int src_x = std::min(
                width_ - 1,
                static_cast<int>(static_cast<double>(ix) / static_cast<double>(plot_w) * width_));
            const bool cropped = crop_mask_entire_ || src_x < crop_h_min_ || src_x > crop_h_max_ ||
                                 src_y < crop_v_min_ || src_y > crop_v_max_;
            if (cropped) {
                img.setPixel(ix, iy, qRgb(0, 0, 0));
                continue;
            }
            const float v = data_[static_cast<std::size_t>(src_y) * static_cast<std::size_t>(width_) +
                                 static_cast<std::size_t>(src_x)];
            const float cv = std::clamp(v, vmin_, vmax_);
            float norm = (cv - vmin_) / (vmax_ - vmin_);
            norm = std::clamp(norm, 0.0f, 1.0f);
            if (!color_lut_.empty()) {
                int idx = static_cast<int>(norm * static_cast<float>(color_lut_.size() - 1));
                idx = std::clamp(idx, 0, static_cast<int>(color_lut_.size()) - 1);
                img.setPixel(ix, iy, color_lut_[static_cast<std::size_t>(idx)]);
            }
        }
    }

    p.drawImage(plot_left, plot_top, img);

    if (!crop_mask_entire_ && crop_h_max_ >= crop_h_min_ && crop_v_max_ >= crop_v_min_ &&
        (crop_h_min_ > 0 || crop_h_max_ < width_ - 1 || crop_v_min_ > 0 || crop_v_max_ < height_ - 1)) {
        const int x0 = plot_left + crop_h_min_ * plot_w / width_;
        const int x1 = plot_left + (crop_h_max_ + 1) * plot_w / width_;
        int y0 = plot_top + crop_v_min_ * plot_h / height_;
        int y1 = plot_top + (crop_v_max_ + 1) * plot_h / height_;
        if (flip_il_vert) {
            const int ty0 = plot_top + (height_ - 1 - crop_v_max_) * plot_h / height_;
            const int ty1 = plot_top + (height_ - crop_v_min_) * plot_h / height_;
            y0 = ty0;
            y1 = ty1;
        }
        p.setPen(QPen(QColor(255, 120, 60), 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0));
    }

    p.setPen(palette().color(QPalette::Text));
    QFont axisFont = p.font();
    axisFont.setPointSize(8);
    QFont titleFont = axisFont;
    titleFont.setBold(true);

    const int axis_top_y = plot_top - 6;
    const int axis_left_x = plot_left - 6;

    p.drawLine(plot_left, axis_top_y, plot_right, axis_top_y);
    p.drawLine(axis_left_x, plot_top, axis_left_x, plot_bottom);

    std::vector<int32_t> horiz_ticks = horiz_labels_;
    if (horiz_ticks.empty() && width_ > 0) {
        horiz_ticks.resize(static_cast<std::size_t>(width_));
        for (int i = 0; i < width_; ++i) {
            horiz_ticks[static_cast<std::size_t>(i)] = i;
        }
    }
    drawTopTicks(p, plot_left, plot_right, axis_top_y, plot_w, horiz_ticks, axisFont);

    if (vert_is_time_ && vertAxisIsTimeMs(mode_)) {
        const int sample_count = height_;
        drawLeftTimeTicks(p, axis_left_x, plot_top, plot_bottom, plot_h, sample_count, vert_step_ms_,
                          axisFont);
    } else {
        std::vector<int32_t> vert_ticks = vert_index_labels_;
        if (vert_ticks.empty() && height_ > 0) {
            vert_ticks.resize(static_cast<std::size_t>(height_));
            for (int i = 0; i < height_; ++i) {
                vert_ticks[static_cast<std::size_t>(i)] = i;
            }
        }
        const bool il_origin_bottom = mode_ == SliceMode::Time;
        drawLeftIndexTicks(p, axis_left_x, plot_top, plot_bottom, plot_h, vert_ticks, axisFont,
                           il_origin_bottom);
    }

    p.setFont(titleFont);
    p.drawText(QRect(plot_left, 2, plot_w, 16), Qt::AlignHCenter | Qt::AlignVCenter,
               horizAxisLabel(mode_));
    p.save();
    p.translate(10, plot_top + plot_h / 2);
    p.rotate(-90.0);
    p.drawText(QRect(-60, -10, 120, 20), Qt::AlignCenter, vertAxisLabel(mode_));
    p.restore();

    if (selection_mode_ && selecting_) {
        const QRect sel = QRect(select_start_, select_end_).normalized();
        p.setPen(QPen(QColor(255, 220, 80), 1.5, Qt::DashLine));
        p.setBrush(QColor(255, 220, 80, 40));
        p.drawRect(sel);
    }
}

void SliceView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update();
}

void SliceView::wheelEvent(QWheelEvent* event) {
    if (selection_mode_) {
        event->ignore();
        return;
    }
    const int delta = event->angleDelta().y() > 0 ? -1 : 1;
    emit sliceScrollRequested(delta);
    event->accept();
}

void SliceView::mousePressEvent(QMouseEvent* event) {
    if (selection_mode_ && event->button() == Qt::LeftButton) {
        const QRect plot = plotPixelRect();
        if (plot.contains(event->pos())) {
            selecting_ = true;
            select_start_ = event->pos();
            select_end_ = event->pos();
            update();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void SliceView::mouseMoveEvent(QMouseEvent* event) {
    if (selection_mode_ && selecting_) {
        select_end_ = event->pos();
        update();
        event->accept();
        return;
    }

    int h = 0, v = 0;
    if (mapPixelToIndices(event->pos().x(), event->pos().y(), h, v)) {
        const float val = data_[static_cast<std::size_t>(v) * static_cast<std::size_t>(width_) +
                            static_cast<std::size_t>(h)];
        emit hoverInfoChanged(h, v, val, true);
    } else {
        emit hoverInfoChanged(0, 0, 0.f, false);
    }
    QWidget::mouseMoveEvent(event);
}

void SliceView::mouseReleaseEvent(QMouseEvent* event) {
    if (selection_mode_ && selecting_ && event->button() == Qt::LeftButton) {
        selecting_ = false;
        const QRect sel = QRect(select_start_, select_end_).normalized();
        update();
        int h0 = 0;
        int h1 = 0;
        int v0 = 0;
        int v1 = 0;
        if (sel.width() > 4 && sel.height() > 4) {
            indicesFromSelection(sel, h0, h1, v0, v1);
        } else if (width_ > 0 && height_ > 0) {
            h0 = 0;
            h1 = width_ - 1;
            v0 = 0;
            v1 = height_ - 1;
        }
        if (width_ > 0 && height_ > 0) {
            emit regionSelected(h0, h1, v0, v1);
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

}  // namespace kubik
