#pragma once

class QAbstractSpinBox;

namespace kubik {

/// Корректная геометрия line edit и обработка кликов по стрелкам (Windows + Fusion).
void setupSpinBox(QAbstractSpinBox* spin);

}  // namespace kubik
