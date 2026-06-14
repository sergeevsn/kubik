#pragma once

#include <QWidget>
#include <QRgb>

#include <vector>

#include "SliceView.hpp"

namespace kubik {

/// Миниатюра 2D среза для превью до/после фильтрации.
class SlicePreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit SlicePreviewWidget(QWidget* parent = nullptr);

    void setSlice(const std::vector<float>& data, int w, int h, float vmin, float vmax);
    void setTitle(const QString& title);
    void setColorMap(ColorMap cmap);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateColorLut();

    std::vector<float> data_;
    int width_ = 0;
    int height_ = 0;
    float vmin_ = 0.f;
    float vmax_ = 1.f;
    ColorMap color_map_ = ColorMap::Grayscale;
    std::vector<QRgb> color_lut_;
    QString title_;
};

}  // namespace kubik
