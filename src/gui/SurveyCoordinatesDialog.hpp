#pragma once

#include <QDialog>

namespace kubik {

class SegyCube;

class SurveyCoordinatesDialog : public QDialog {
    Q_OBJECT
public:
    explicit SurveyCoordinatesDialog(const SegyCube& cube, QWidget* parent = nullptr);

private:
    void setupUi(const SegyCube& cube);
};

}  // namespace kubik
