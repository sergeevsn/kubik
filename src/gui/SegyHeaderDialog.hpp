#pragma once

#include <QDialog>

namespace kubik {

class SegyCube;

class SegyHeaderDialog : public QDialog {
    Q_OBJECT
public:
    explicit SegyHeaderDialog(const SegyCube& cube, QWidget* parent = nullptr);

private:
    void setupUi(const SegyCube& cube);
};

}  // namespace kubik
