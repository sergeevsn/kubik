#pragma once

#include <QWidget>

#include <vector>

#include "kubik/SegyCube.hpp"

namespace kubik {

/// Карта съёмки: CDP-точки, описывающий прямоугольник и угловые точки.
class SurveyMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit SurveyMapWidget(QWidget* parent = nullptr);

    void setSurveyData(const SurveyCoordinateStats& stats);

    QSize sizeHint() const override;
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override { return true; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct PlotLayout {
        QRectF area;
        QRectF plot;
    };

    QRectF plotArea() const;
    QRectF dataBounds() const;
    PlotLayout computePlotLayout(const QRectF& bounds) const;
    QPointF mapToScreen(double x, double y, const PlotLayout& layout, const QRectF& bounds) const;

    std::vector<SurveyCdpPoint> points_;
    std::vector<SurveyCornerPoint> corners_;
};

}  // namespace kubik
