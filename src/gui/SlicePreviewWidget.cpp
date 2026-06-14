#include "SlicePreviewWidget.hpp"
#include "ColorSchemes.hpp"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace kubik {

SlicePreviewWidget::SlicePreviewWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(120, 200);
    updateColorLut();
}

void SlicePreviewWidget::setColorMap(ColorMap cmap) {
    color_map_ = cmap;
    updateColorLut();
    update();
}

void SlicePreviewWidget::updateColorLut() {
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

void SlicePreviewWidget::setSlice(const std::vector<float>& data, int w, int h, float vmin, float vmax) {
    data_ = data;
    width_ = w;
    height_ = h;
    vmin_ = vmin;
    vmax_ = vmax;
    if (!(vmax_ > vmin_)) {
        vmax_ = vmin_ + 1.f;
    }
    update();
    repaint();
}

void SlicePreviewWidget::setTitle(const QString& title) {
    title_ = title;
    update();
}

void SlicePreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().brush(QPalette::Window));

    const int title_h = title_.isEmpty() ? 0 : 16;
    const QRect img_rect(4, title_h + 2, width() - 8, height() - title_h - 6);
    if (data_.empty() || width_ <= 0 || height_ <= 0 || img_rect.width() <= 0 || img_rect.height() <= 0) {
        p.setPen(palette().color(QPalette::Text));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("—"));
        return;
    }

    QImage img(img_rect.width(), img_rect.height(), QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
        const int sy = std::min(height_ - 1, y * height_ / img.height());
        for (int x = 0; x < img.width(); ++x) {
            const int sx = std::min(width_ - 1, x * width_ / img.width());
            const float v = data_[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width_) +
                                static_cast<std::size_t>(sx)];
            const float norm = std::clamp((v - vmin_) / (vmax_ - vmin_), 0.f, 1.f);
            const int li = std::clamp(static_cast<int>(norm * static_cast<float>(color_lut_.size() - 1)), 0,
                                      static_cast<int>(color_lut_.size()) - 1);
            img.setPixel(x, y, color_lut_[static_cast<std::size_t>(li)]);
        }
    }
    p.drawImage(img_rect.topLeft(), img);

    if (!title_.isEmpty()) {
        p.setPen(palette().color(QPalette::Text));
        QFont f = p.font();
        f.setPointSize(8);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(4, 0, width() - 8, title_h), Qt::AlignHCenter | Qt::AlignVCenter, title_);
    }
}

}  // namespace kubik
