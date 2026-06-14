#pragma once

#include <QWidget>
#include <QRgb>

#include <cstdint>
#include <vector>

#include "kubik/types.hpp"

namespace kubik {

enum class ColorMap {
    Grayscale = 0,
    Viridis,
    RedBlue,
    PetrelClassic,
    Kingdom
};

/// Виджет отрисовки 2D среза куба.
class SliceView : public QWidget {
    Q_OBJECT
public:
    explicit SliceView(QWidget* parent = nullptr);

    void setSlice(const std::vector<float>& data, int width, int height, float vmin, float vmax);
    void setSliceMode(SliceMode mode);
    void setAxisLabels(std::vector<int32_t> horiz_labels,
                       std::vector<int32_t> vert_index_labels,
                       bool vert_is_time,
                       float vert_step_ms);
    void setColorMap(ColorMap cmap);
    void setCropRanges(int h_min, int h_max, int v_min, int v_max, bool mask_entire);
    void setSelectionMode(bool enabled);
    bool selectionMode() const { return selection_mode_; }

signals:
    void sliceScrollRequested(int delta);
    void hoverInfoChanged(int horiz_idx, int vert_idx, float value, bool valid);
    void regionSelected(int h0, int h1, int v0, int v1);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateColorLut();
    bool mapPixelToIndices(int px, int py, int& out_h, int& out_v) const;
    QRect plotPixelRect() const;
    void indicesFromSelection(const QRect& sel_px, int& h0, int& h1, int& v0, int& v1) const;

    std::vector<float> data_;
    int width_ = 0;
    int height_ = 0;
    float vmin_ = 0.f;
    float vmax_ = 1.f;
    SliceMode mode_ = SliceMode::Inline;
    ColorMap color_map_ = ColorMap::Grayscale;

    std::vector<int32_t> horiz_labels_;
    std::vector<int32_t> vert_index_labels_;
    bool vert_is_time_ = true;
    float vert_step_ms_ = 1.f;

    bool crop_mask_entire_ = false;
    int crop_h_min_ = 0;
    int crop_h_max_ = -1;
    int crop_v_min_ = 0;
    int crop_v_max_ = -1;

    std::vector<QRgb> color_lut_;

    bool selection_mode_ = false;
    bool selecting_ = false;
    QPoint select_start_;
    QPoint select_end_;
};

}  // namespace kubik
