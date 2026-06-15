#pragma once

#include <QMainWindow>

#include <memory>

#include "kubik/FftFilter.hpp"
#include "kubik/SegyCube.hpp"
#include "kubik/types.hpp"
#include "SliceView.hpp"

class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;
class QScrollBar;
class QDoubleSpinBox;
class QSpinBox;
class QTextEdit;

namespace kubik {

class CubeNavigator;
class DiscreteValueSpinBox;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void openFileLazy();
    void openFileInMemory();
    void saveFileAs();
    void showHeaders();
    void showCoordinates();
    void showAbout();
    void showHelpLoading();
    void showHelpTools();
    void onSliceScroll(int delta);
    void onNavigatorInline(int il_idx);
    void onNavigatorCrossline(int xl_idx);
    void onNavigatorTime(int t_idx);
    void onSliceSlider(int value);

private:
    void setupUi();
    void applyDarkTheme();
    void loadSegy(const QString& path, CubeLoadMode mode);
    CubeLoadMode resolveLoadMode(const QString& path, CubeLoadMode requested) const;
    void setSliceMode(SliceMode mode);
    void refreshSlice();
    void updateClipRangeLabel();
    int currentSliceIndex() const;
    void setCurrentSliceIndex(int idx);
    int maxSliceIndex() const;
    void updateSliceSpinbox();
    void resetCropBounds();
    void updateCropSpinboxes();
    void applyCropMask(int w, int h, const std::vector<int32_t>& horiz_labels,
                       const std::vector<int32_t>& vert_labels, bool vert_is_time);
    CropBounds fullCropBounds() const;
    void onCropChanged();
    void onCropEnableToggled(bool enabled);
    void onResampleChanged();
    void onResampleEnableToggled(bool enabled);
    void onFftSelectToggled(bool enabled);
    void onFootprintSelectToggled(bool enabled);
    void onFftRegionSelected(int h0, int h1, int v0, int v1);
    void onFftApplyToCube(const FftFilterParams& params);
    void onFootprintApplyToCube(const FftFilter2DParams& params);
    void onFftFilterEnableToggled(bool enabled);
    void onFootprintFilterEnableToggled(bool enabled);
    void updateFilterToolState();
    void updateFftCubeFilterLabel();
    void updateFootprintCubeFilterLabel();
    void applyCubeFftFilter(std::vector<float>& data, int w, int h, float dt_ms);
    void applyCubeFft2DFilter(std::vector<float>& data, int w, int h,
                              const std::vector<int32_t>& horiz_labels,
                              const std::vector<int32_t>& vert_labels);
    void updateStatusBase();
    void resetResampleSpinboxes();
    void updateResampleSpinboxes();
    CropBounds currentCropBounds() const;
    ResampleParams currentResampleParams() const;

    std::unique_ptr<SegyCube> cube_;
    SliceMode mode_ = SliceMode::Inline;
    int il_idx_ = 0;
    int xl_idx_ = 0;
    int t_idx_ = 0;
    ColorMap color_map_ = ColorMap::Kingdom;
    float clip_percent_ = 99.f;

    int crop_il_min_ = 0;
    int crop_il_max_ = 0;
    int crop_xl_min_ = 0;
    int crop_xl_max_ = 0;
    int crop_t_min_ = 0;
    int crop_t_max_ = 0;
    bool crop_enabled_ = true;
    bool resample_enabled_ = true;
    int slice_w_ = 0;
    int slice_h_ = 0;

    SliceView* slice_view_ = nullptr;
    CubeNavigator* navigator_ = nullptr;
    QScrollBar* slice_scroll_ = nullptr;
    DiscreteValueSpinBox* slice_spin_ = nullptr;
    QLabel* status_label_ = nullptr;
    QComboBox* palette_combo_ = nullptr;
    QSpinBox* clip_spin_ = nullptr;
    QLabel* clip_range_label_ = nullptr;

    QPushButton* btn_inline_ = nullptr;
    QPushButton* btn_crossline_ = nullptr;
    QPushButton* btn_time_ = nullptr;

    DiscreteValueSpinBox* crop_il_min_spin_ = nullptr;
    DiscreteValueSpinBox* crop_il_max_spin_ = nullptr;
    DiscreteValueSpinBox* crop_xl_min_spin_ = nullptr;
    DiscreteValueSpinBox* crop_xl_max_spin_ = nullptr;
    DiscreteValueSpinBox* crop_t_min_spin_ = nullptr;
    DiscreteValueSpinBox* crop_t_max_spin_ = nullptr;
    QCheckBox* crop_enable_ = nullptr;

    QDoubleSpinBox* resample_dt_spin_ = nullptr;
    QDoubleSpinBox* resample_dil_spin_ = nullptr;
    QDoubleSpinBox* resample_dxl_spin_ = nullptr;
    QCheckBox* resample_enable_ = nullptr;

    QPushButton* btn_fft_select_ = nullptr;
    QCheckBox* fft_filter_enable_ = nullptr;
    QLabel* fft_cube_filter_label_ = nullptr;

    QPushButton* btn_footprint_select_ = nullptr;
    QCheckBox* footprint_filter_enable_ = nullptr;
    QLabel* footprint_cube_filter_label_ = nullptr;

    bool cube_fft_params_set_ = false;
    bool cube_footprint_params_set_ = false;
    bool cube_fft_enabled_ = false;
    bool cube_footprint_enabled_ = false;
    FftFilterParams cube_fft_params_{};
    FftFilter2DParams cube_footprint_params_{};

    QString base_status_;
};

}  // namespace kubik
