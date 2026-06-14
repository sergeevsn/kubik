#include "MainWindow.hpp"
#include "CollapsibleGroupBox.hpp"
#include "CubeNavigator.hpp"
#include "FftFilter2DDialog.hpp"
#include "FftFilterDialog.hpp"
#include "DiscreteValueSpinBox.hpp"
#include "SegyHeaderDialog.hpp"
#include "SurveyCoordinatesDialog.hpp"

#include "kubik/FftFilter.hpp"
#include "kubik/Resample.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QApplication>
#include <QElapsedTimer>

#include <segyio/segy.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <cstring>

namespace kubik {

namespace {

int findFirstIndexAtOrAbove(const std::vector<int32_t>& labels, int32_t value) {
    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        if (labels[static_cast<std::size_t>(i)] >= value) {
            return i;
        }
    }
    return std::max(0, static_cast<int>(labels.size()) - 1);
}

int findLastIndexAtOrBelow(const std::vector<int32_t>& labels, int32_t value) {
    for (int i = static_cast<int>(labels.size()) - 1; i >= 0; --i) {
        if (labels[static_cast<std::size_t>(i)] <= value) {
            return i;
        }
    }
    return 0;
}

void timeCropRows(int t_min_idx, int t_max_idx, const std::vector<int32_t>& vert_labels,
                  bool vert_is_time, int time_ms_lo, int time_ms_hi, int height, int& v_min,
                  int& v_max) {
    v_min = 0;
    v_max = std::max(0, height - 1);
    if (height <= 0) {
        return;
    }
    if (!vert_is_time || vert_labels.empty()) {
        v_min = std::clamp(t_min_idx, 0, height - 1);
        v_max = std::clamp(t_max_idx, 0, height - 1);
        if (v_min > v_max) {
            std::swap(v_min, v_max);
        }
        return;
    }
    v_min = findFirstIndexAtOrAbove(vert_labels, time_ms_lo);
    v_max = findLastIndexAtOrBelow(vert_labels, time_ms_hi);
    v_min = std::clamp(v_min, 0, height - 1);
    v_max = std::clamp(v_max, 0, height - 1);
    if (v_min > v_max) {
        std::swap(v_min, v_max);
    }
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), cube_(std::make_unique<SegyCube>()) {
    applyDarkTheme();
    setupUi();
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("Kubik"));
    resize(1280, 800);
}

void MainWindow::applyDarkTheme() {
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(45, 45, 48));
    pal.setColor(QPalette::WindowText, QColor(220, 220, 220));
    pal.setColor(QPalette::Base, QColor(30, 30, 32));
    pal.setColor(QPalette::AlternateBase, QColor(50, 50, 54));
    pal.setColor(QPalette::Text, QColor(220, 220, 220));
    pal.setColor(QPalette::Button, QColor(60, 60, 64));
    pal.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    pal.setColor(QPalette::Highlight, QColor(0, 120, 215));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    setPalette(pal);
}

void MainWindow::setupUi() {
    auto* fileMenu = menuBar()->addMenu(tr("&Файл"));
    auto* openAct = fileMenu->addAction(tr("&Открыть SEG-Y..."));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openFile);

    auto* saveAsAct = fileMenu->addAction(tr("Сохранить как..."));
    saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::saveFileAs);

    auto* infoMenu = menuBar()->addMenu(tr("&Инфо"));
    auto* headersAct = infoMenu->addAction(tr("Заголовки SEG-Y..."));
    connect(headersAct, &QAction::triggered, this, &MainWindow::showHeaders);

    auto* coordsAct = infoMenu->addAction(tr("Координаты..."));
    connect(coordsAct, &QAction::triggered, this, &MainWindow::showCoordinates);

    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* centerCol = new QVBoxLayout();
    centerCol->setContentsMargins(0, 0, 0, 0);

    auto* viewRow = new QHBoxLayout();
    slice_view_ = new SliceView(central);
    slice_scroll_ = new QScrollBar(Qt::Vertical, central);
    slice_scroll_->setEnabled(false);
    connect(slice_scroll_, &QScrollBar::valueChanged, this, &MainWindow::onSliceSlider);
    connect(slice_view_, &SliceView::sliceScrollRequested, this, &MainWindow::onSliceScroll);
    connect(slice_view_, &SliceView::regionSelected, this, &MainWindow::onFftRegionSelected);
    connect(slice_view_, &SliceView::hoverInfoChanged, this,
            [this](int h, int v, float val, bool valid) {
                if (!status_label_) return;
                if (!valid || !cube_->isLoaded()) {
                    status_label_->setText(base_status_);
                    return;
                }
                const auto& g = cube_->geometry();
                QString horiz, vert;
                if (mode_ == SliceMode::Inline) {
                    horiz = tr("XL %1").arg(cube_->crosslineLabel(h));
                    vert = tr("T %1 ms").arg(v * g.dt_ms, 0, 'f', 1);
                } else if (mode_ == SliceMode::Crossline) {
                    horiz = tr("IL %1").arg(cube_->inlineLabel(h));
                    vert = tr("T %1 ms").arg(v * g.dt_ms, 0, 'f', 1);
                } else {
                    horiz = tr("XL %1").arg(cube_->crosslineLabel(h));
                    vert = tr("IL %1").arg(cube_->inlineLabel(v));
                }
                status_label_->setText(base_status_ + QStringLiteral(" | ") + horiz + QStringLiteral(" | ") +
                                       vert + tr(" | Amp %1").arg(val, 0, 'g', 5));
            });

    viewRow->addWidget(slice_view_, 1);
    viewRow->addWidget(slice_scroll_, 0);
    centerCol->addLayout(viewRow, 1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(10, 6, 10, 10);
    palette_combo_ = new QComboBox(central);
    palette_combo_->addItem(tr("Grayscale"), static_cast<int>(ColorMap::Grayscale));
    palette_combo_->addItem(tr("Viridis"), static_cast<int>(ColorMap::Viridis));
    palette_combo_->addItem(tr("Red/Blue"), static_cast<int>(ColorMap::RedBlue));
    palette_combo_->addItem(tr("Petrel"), static_cast<int>(ColorMap::PetrelClassic));
    palette_combo_->addItem(tr("Kingdom"), static_cast<int>(ColorMap::Kingdom));
    palette_combo_->setCurrentIndex(4);  // Kingdom
    connect(palette_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        color_map_ = static_cast<ColorMap>(palette_combo_->currentData().toInt());
        if (slice_view_) slice_view_->setColorMap(color_map_);
        refreshSlice();
    });
    bottomRow->addWidget(new QLabel(tr("Палитра:"), central));
    bottomRow->addWidget(palette_combo_);

    clip_spin_ = new QSpinBox(central);
    clip_spin_->setRange(1, 100);
    clip_spin_->setValue(static_cast<int>(clip_percent_));
    clip_spin_->setSuffix(QStringLiteral("%"));
    clip_spin_->setToolTip(tr("Clip: ширина диапазона в %% распределения; 99 → [p0.5, p99.5], 1 → узкий центр"));
    clip_range_label_ = new QLabel(central);
    clip_range_label_->setMinimumWidth(140);
    connect(clip_spin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        clip_percent_ = static_cast<float>(v);
        updateClipRangeLabel();
        refreshSlice();
    });

    bottomRow->addSpacing(16);
    bottomRow->addWidget(new QLabel(tr("Clip:"), central));
    bottomRow->addWidget(clip_spin_);
    bottomRow->addWidget(clip_range_label_);
    bottomRow->addStretch(1);
    centerCol->addLayout(bottomRow);

    auto* rightCol = new QVBoxLayout();
    rightCol->setSpacing(6);
    rightCol->setContentsMargins(10, 8, 14, 8);

    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(4);
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);

    btn_inline_ = new QPushButton(tr("IL"), central);
    btn_crossline_ = new QPushButton(tr("XL"), central);
    btn_time_ = new QPushButton(tr("T"), central);
    for (QPushButton* btn : {btn_inline_, btn_crossline_, btn_time_}) {
        btn->setCheckable(true);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        modeGroup->addButton(btn);
        modeRow->addWidget(btn);
    }
    btn_inline_->setToolTip(tr("InLine (I)"));
    btn_crossline_->setToolTip(tr("CrossLine (X)"));
    btn_time_->setToolTip(tr("Time slice (T)"));
    btn_inline_->setChecked(true);
    connect(btn_inline_, &QPushButton::clicked, this, [this]() { setSliceMode(SliceMode::Inline); });
    connect(btn_crossline_, &QPushButton::clicked, this, [this]() { setSliceMode(SliceMode::Crossline); });
    connect(btn_time_, &QPushButton::clicked, this, [this]() { setSliceMode(SliceMode::Time); });
    rightCol->addLayout(modeRow);

    slice_spin_ = new DiscreteValueSpinBox(central);
    slice_spin_->setEnabled(false);
    connect(slice_spin_, &DiscreteValueSpinBox::currentIndexChanged, this, &MainWindow::onSliceSlider);
    rightCol->addWidget(new QLabel(tr("Срез:"), central));
    rightCol->addWidget(slice_spin_);

    navigator_ = new CubeNavigator(central);
    connect(navigator_, &CubeNavigator::inlineJumpRequested, this, &MainWindow::onNavigatorInline);
    connect(navigator_, &CubeNavigator::crosslineJumpRequested, this, &MainWindow::onNavigatorCrossline);
    connect(navigator_, &CubeNavigator::timeJumpRequested, this, &MainWindow::onNavigatorTime);
    navigator_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    rightCol->addWidget(navigator_, 0);

    auto* crop_section = new CollapsibleGroupBox(tr("Обрезка"), central);
    crop_section->setExpanded(true);
    auto* crop_body = crop_section->contentWidget();
    auto* crop_layout = new QVBoxLayout(crop_body);
    crop_layout->setContentsMargins(4, 0, 0, 4);
    crop_layout->setSpacing(4);

    crop_enable_ = new QCheckBox(tr("Crop включён"), crop_body);
    crop_enable_->setEnabled(false);
    crop_enable_->setToolTip(tr("Вкл/выкл кроп на срезах и при сохранении SEG-Y"));
    crop_layout->addWidget(crop_enable_);
    connect(crop_enable_, &QCheckBox::toggled, this, &MainWindow::onCropEnableToggled);

    auto* crop_grid = new QGridLayout();
    crop_grid->setHorizontalSpacing(6);
    crop_grid->setVerticalSpacing(4);

    auto addCropRow = [&](int row, const QString& label, DiscreteValueSpinBox*& min_spin,
                          DiscreteValueSpinBox*& max_spin) {
        crop_grid->addWidget(new QLabel(label, crop_body), row, 0);
        min_spin = new DiscreteValueSpinBox(crop_body);
        max_spin = new DiscreteValueSpinBox(crop_body);
        min_spin->setMinimumWidth(112);
        max_spin->setMinimumWidth(112);
        min_spin->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        max_spin->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        min_spin->setEnabled(false);
        max_spin->setEnabled(false);
        crop_grid->addWidget(new QLabel(tr("min"), crop_body), row, 1);
        crop_grid->addWidget(min_spin, row, 2);
        crop_grid->addWidget(new QLabel(tr("max"), crop_body), row, 3);
        crop_grid->addWidget(max_spin, row, 4);
        connect(min_spin, &DiscreteValueSpinBox::currentIndexChanged, this, &MainWindow::onCropChanged);
        connect(max_spin, &DiscreteValueSpinBox::currentIndexChanged, this, &MainWindow::onCropChanged);
    };

    addCropRow(0, tr("IL"), crop_il_min_spin_, crop_il_max_spin_);
    addCropRow(1, tr("XL"), crop_xl_min_spin_, crop_xl_max_spin_);
    addCropRow(2, tr("T"), crop_t_min_spin_, crop_t_max_spin_);
    crop_grid->setColumnStretch(2, 1);
    crop_grid->setColumnStretch(4, 1);
    crop_layout->addLayout(crop_grid);
    rightCol->addWidget(crop_section, 0);

    auto* resample_section = new CollapsibleGroupBox(tr("Ресэмпинг"), central);
    resample_section->setExpanded(false);
    auto* resample_body = resample_section->contentWidget();
    auto* resample_layout = new QVBoxLayout(resample_body);
    resample_layout->setContentsMargins(4, 0, 0, 4);
    resample_layout->setSpacing(6);

    resample_enable_ = new QCheckBox(tr("Resample включён"), resample_body);
    resample_enable_->setEnabled(false);
    resample_enable_->setToolTip(tr("Вкл/выкл ресемплинг на срезах и при сохранении SEG-Y"));
    resample_layout->addWidget(resample_enable_);
    connect(resample_enable_, &QCheckBox::toggled, this, &MainWindow::onResampleEnableToggled);

    auto* time_box = new QGroupBox(tr("Time"), resample_body);
    auto* time_form = new QGridLayout(time_box);
    resample_dt_spin_ = new QDoubleSpinBox(time_box);
    resample_dt_spin_->setDecimals(2);
    resample_dt_spin_->setRange(0.01, 10000.0);
    resample_dt_spin_->setSuffix(tr(" ms"));
    resample_dt_spin_->setEnabled(false);
    resample_dt_spin_->setToolTip(tr("Потрассовый ресемплинг по времени (Delta T)"));
    time_form->addWidget(new QLabel(tr("Delta T"), time_box), 0, 0);
    time_form->addWidget(resample_dt_spin_, 0, 1);
    resample_layout->addWidget(time_box);

    auto* space_box = new QGroupBox(tr("Space"), resample_body);
    auto* space_form = new QGridLayout(space_box);
    resample_dil_spin_ = new QDoubleSpinBox(space_box);
    resample_dxl_spin_ = new QDoubleSpinBox(space_box);
    for (QDoubleSpinBox* spin : {resample_dil_spin_, resample_dxl_spin_}) {
        spin->setDecimals(0);
        spin->setRange(0.0, 1000000.0);
        spin->setEnabled(false);
    }
    resample_dil_spin_->setToolTip(tr("Шаг сетки по Inline"));
    resample_dxl_spin_->setToolTip(tr("Шаг сетки по Crossline"));
    space_form->addWidget(new QLabel(tr("Delta Inline"), space_box), 0, 0);
    space_form->addWidget(resample_dil_spin_, 0, 1);
    space_form->addWidget(new QLabel(tr("Delta Crossline"), space_box), 1, 0);
    space_form->addWidget(resample_dxl_spin_, 1, 1);
    resample_layout->addWidget(space_box);

    connect(resample_dt_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::onResampleChanged);
    connect(resample_dil_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::onResampleChanged);
    connect(resample_dxl_spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::onResampleChanged);

    rightCol->addWidget(resample_section, 0);

    auto* fft_section = new CollapsibleGroupBox(tr("FFT Фильтрация"), central);
    fft_section->setExpanded(false);
    auto* fft_body = fft_section->contentWidget();
    auto* fft_layout = new QVBoxLayout(fft_body);
    fft_layout->setContentsMargins(4, 0, 0, 4);

    auto* fft_hint = new QLabel(tr("1D FFT по времени на срезах Inline / Crossline"), fft_body);
    fft_hint->setWordWrap(true);
    fft_layout->addWidget(fft_hint);

    btn_fft_select_ = new QPushButton(tr("Настройка"), fft_body);
    btn_fft_select_->setCheckable(true);
    btn_fft_select_->setEnabled(false);
    btn_fft_select_->setToolTip(
        tr("Клик по срезу — анализ всего среза; перетаскивание — выделенная область"));
    fft_layout->addWidget(btn_fft_select_);
    connect(btn_fft_select_, &QPushButton::toggled, this, &MainWindow::onFftSelectToggled);

    fft_filter_enable_ = new QCheckBox(tr("Фильтр включён"), fft_body);
    fft_filter_enable_->setEnabled(false);
    fft_filter_enable_->setToolTip(
        tr("Вкл/выкл 1D FFT-фильтр на срезах IL/XL и при сохранении SEG-Y"));
    fft_layout->addWidget(fft_filter_enable_);
    connect(fft_filter_enable_, &QCheckBox::toggled, this, &MainWindow::onFftFilterEnableToggled);

    fft_cube_filter_label_ = new QLabel(tr("Фильтр к кубу: нет"), fft_body);
    fft_cube_filter_label_->setWordWrap(true);
    fft_layout->addWidget(fft_cube_filter_label_);

    rightCol->addWidget(fft_section, 0);

    auto* footprint_section = new CollapsibleGroupBox(tr("Футпринты"), central);
    footprint_section->setExpanded(false);
    auto* footprint_body = footprint_section->contentWidget();
    auto* footprint_layout = new QVBoxLayout(footprint_body);
    footprint_layout->setContentsMargins(4, 0, 0, 4);

    auto* footprint_hint =
        new QLabel(tr("2D footprint-фильтр k_IL×k_XL на Time-срезах"), footprint_body);
    footprint_hint->setWordWrap(true);
    footprint_layout->addWidget(footprint_hint);

    btn_footprint_select_ = new QPushButton(tr("Настройка"), footprint_body);
    btn_footprint_select_->setCheckable(true);
    btn_footprint_select_->setEnabled(false);
    btn_footprint_select_->setToolTip(
        tr("Клик по срезу — анализ всего среза; перетаскивание — выделенная область"));
    footprint_layout->addWidget(btn_footprint_select_);
    connect(btn_footprint_select_, &QPushButton::toggled, this, &MainWindow::onFootprintSelectToggled);

    footprint_filter_enable_ = new QCheckBox(tr("Фильтр включён"), footprint_body);
    footprint_filter_enable_->setEnabled(false);
    footprint_filter_enable_->setToolTip(
        tr("Вкл/выкл footprint-фильтр на Time-срезах и при сохранении SEG-Y"));
    footprint_layout->addWidget(footprint_filter_enable_);
    connect(footprint_filter_enable_, &QCheckBox::toggled, this,
            &MainWindow::onFootprintFilterEnableToggled);

    footprint_cube_filter_label_ = new QLabel(tr("Фильтр к кубу: нет"), footprint_body);
    footprint_cube_filter_label_->setWordWrap(true);
    footprint_layout->addWidget(footprint_cube_filter_label_);

    rightCol->addWidget(footprint_section, 0);
    rightCol->addStretch(1);

    root->setSpacing(6);
    root->addLayout(centerCol, 1);
    root->addLayout(rightCol, 0);
    setCentralWidget(central);

    status_label_ = new QLabel(this);
    statusBar()->addWidget(status_label_, 1);
    updateStatusBase();
}

void MainWindow::openFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Открыть SEG-Y"), QString(),
        tr("SEG-Y (*.sgy *.segy);;Все файлы (*)"));
    if (!path.isEmpty()) {
        loadSegy(path);
    }
}

void MainWindow::saveFileAs() {
    if (!cube_->isLoaded()) {
        QMessageBox::information(this, tr("Сохранить"), tr("Сначала откройте SEG-Y файл."));
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("Сохранить SEG-Y"), QString(),
        tr("SEG-Y (*.sgy *.segy);;Все файлы (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".sgy"), Qt::CaseInsensitive) &&
        !path.endsWith(QStringLiteral(".segy"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".sgy");
    }

    QProgressDialog progress(tr("Сохранение SEG-Y"), tr("Отмена"), 0, 1, this);
    progress.setWindowTitle(tr("Сохранение SEG-Y"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);
    progress.show();
    QApplication::processEvents();

    QElapsedTimer elapsed;
    elapsed.start();
    qint64 last_ui_ms = -1000;

    auto stageName = [this](SaveCroppedProgress::Stage stage) -> QString {
        switch (stage) {
        case SaveCroppedProgress::Stage::Prepare:
            return tr("Подготовка");
        case SaveCroppedProgress::Stage::ReadTraces:
            return tr("Чтение и ресемплинг трасс");
        case SaveCroppedProgress::Stage::SpatialResample:
            return tr("Пространственный ресемплинг");
        case SaveCroppedProgress::Stage::Fft1D:
            return tr("FFT 1D фильтр");
        case SaveCroppedProgress::Stage::Fft2D:
            return tr("FFT 2D footprint");
        case SaveCroppedProgress::Stage::WriteSegy:
            return tr("Запись SEG-Y");
        }
        return {};
    };

    auto formatElapsed = [](qint64 ms) -> QString {
        const int sec = static_cast<int>(ms / 1000);
        return QStringLiteral("%1:%2")
            .arg(sec / 60, 2, 10, QChar('0'))
            .arg(sec % 60, 2, 10, QChar('0'));
    };

    const SaveCroppedProgressCallback progress_cb = [&](const SaveCroppedProgress& info) -> bool {
        const qint64 now = elapsed.elapsed();
        const bool force_update = info.overall_current >= info.overall_total || info.stage_current <= 1 ||
                                  info.stage_current >= info.stage_total || now - last_ui_ms >= 100;
        if (!force_update) {
            return !progress.wasCanceled();
        }
        last_ui_ms = now;

        const int pct = info.overall_total > 0
                            ? static_cast<int>((100LL * info.overall_current) / info.overall_total)
                            : 0;
        progress.setMaximum(info.overall_total);
        progress.setValue(std::min(info.overall_current, info.overall_total));
        progress.setLabelText(tr("%1: %2 / %3\n%4% — %5")
                                  .arg(stageName(info.stage))
                                  .arg(info.stage_current)
                                  .arg(info.stage_total)
                                  .arg(pct)
                                  .arg(formatElapsed(now)));
        QApplication::processEvents();
        return !progress.wasCanceled();
    };

    try {
        const FftFilterParams* fft =
            (cube_fft_params_set_ && cube_fft_enabled_) ? &cube_fft_params_ : nullptr;
        const FftFilter2DParams* footprint =
            (cube_footprint_params_set_ && cube_footprint_enabled_) ? &cube_footprint_params_ : nullptr;
        cube_->saveCropped(path.toStdString(), currentCropBounds(), currentResampleParams(), fft, footprint,
                           progress_cb);
    } catch (const SaveCanceled&) {
        progress.close();
        statusBar()->showMessage(tr("Сохранение отменено"), 3000);
        return;
    } catch (const std::exception& ex) {
        progress.close();
        QMessageBox::critical(this, tr("Ошибка сохранения"), QString::fromUtf8(ex.what()));
        return;
    }

    progress.setValue(progress.maximum());
    progress.close();

    statusBar()->showMessage(tr("Сохранено %1").arg(QFileInfo(path).fileName()), 5000);
}

void MainWindow::showHeaders() {
    if (!cube_->isLoaded()) {
        QMessageBox::information(this, tr("Заголовки"), tr("Сначала откройте SEG-Y файл."));
        return;
    }
    auto* dlg = new SegyHeaderDialog(*cube_, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::showCoordinates() {
    if (!cube_->isLoaded()) {
        QMessageBox::information(this, tr("Координаты"), tr("Сначала откройте SEG-Y файл."));
        return;
    }
    auto* dlg = new SurveyCoordinatesDialog(*cube_, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::loadSegy(const QString& path) {
    try {
        cube_->load(path.toStdString());
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, tr("Ошибка"), QString::fromUtf8(ex.what()));
        return;
    }

    const auto& g = cube_->geometry();
    il_idx_ = g.n_il / 2;
    xl_idx_ = g.n_xl / 2;
    t_idx_ = g.n_t / 2;

    navigator_->setCubeSize(g.n_il, g.n_xl, g.n_t);
    navigator_->setSlicePositions(il_idx_, xl_idx_, t_idx_);

    slice_scroll_->setEnabled(maxSliceIndex() > 0);
    slice_scroll_->setMinimum(0);
    slice_scroll_->setMaximum(maxSliceIndex());
    slice_scroll_->setValue(currentSliceIndex());

    slice_spin_->setEnabled(true);
    updateSliceSpinbox();
    resetCropBounds();
    updateCropSpinboxes();
    resetResampleSpinboxes();
    updateResampleSpinboxes();

    crop_enabled_ = true;
    resample_enabled_ = true;
    if (crop_enable_) {
        crop_enable_->blockSignals(true);
        crop_enable_->setEnabled(true);
        crop_enable_->setChecked(true);
        crop_enable_->blockSignals(false);
    }
    if (resample_enable_) {
        resample_enable_->blockSignals(true);
        resample_enable_->setEnabled(true);
        resample_enable_->setChecked(true);
        resample_enable_->blockSignals(false);
    }

    cube_fft_params_set_ = false;
    cube_footprint_params_set_ = false;
    cube_fft_enabled_ = false;
    cube_footprint_enabled_ = false;
    if (fft_filter_enable_) {
        fft_filter_enable_->blockSignals(true);
        fft_filter_enable_->setChecked(false);
        fft_filter_enable_->setEnabled(false);
        fft_filter_enable_->blockSignals(false);
    }
    if (footprint_filter_enable_) {
        footprint_filter_enable_->blockSignals(true);
        footprint_filter_enable_->setChecked(false);
        footprint_filter_enable_->setEnabled(false);
        footprint_filter_enable_->blockSignals(false);
    }
    updateFftCubeFilterLabel();
    updateFootprintCubeFilterLabel();
    updateStatusBase();
    updateClipRangeLabel();
    setSliceMode(SliceMode::Inline);
    updateFilterToolState();
    statusBar()->showMessage(tr("Загружен %1").arg(QFileInfo(path).fileName()), 3000);
}

void MainWindow::setSliceMode(SliceMode mode) {
    mode_ = mode;
    if (btn_inline_) btn_inline_->setChecked(mode == SliceMode::Inline);
    if (btn_crossline_) btn_crossline_->setChecked(mode == SliceMode::Crossline);
    if (btn_time_) btn_time_->setChecked(mode == SliceMode::Time);
    if (navigator_) navigator_->setSliceMode(mode);
    if (slice_view_) slice_view_->setSliceMode(mode);

    const int maxIdx = maxSliceIndex();
    slice_scroll_->setMaximum(maxIdx);
    slice_scroll_->setValue(currentSliceIndex());
    updateSliceSpinbox();
    updateFilterToolState();
    refreshSlice();
}

int MainWindow::currentSliceIndex() const {
    switch (mode_) {
    case SliceMode::Inline: return il_idx_;
    case SliceMode::Crossline: return xl_idx_;
    case SliceMode::Time: return t_idx_;
    }
    return 0;
}

void MainWindow::setCurrentSliceIndex(int idx) {
    const int maxIdx = maxSliceIndex();
    idx = std::clamp(idx, 0, maxIdx);
    switch (mode_) {
    case SliceMode::Inline: il_idx_ = idx; break;
    case SliceMode::Crossline: xl_idx_ = idx; break;
    case SliceMode::Time: t_idx_ = idx; break;
    }
    if (navigator_) navigator_->setSlicePositions(il_idx_, xl_idx_, t_idx_);
}

void MainWindow::updateSliceSpinbox() {
    if (!slice_spin_ || !cube_->isLoaded()) {
        return;
    }

    const auto& g = cube_->geometry();
    QVector<int> values;
    int current_idx = 0;

    switch (mode_) {
    case SliceMode::Inline:
        values.reserve(g.n_il);
        for (int32_t v : cube_->inlines()) {
            values.append(v);
        }
        current_idx = il_idx_;
        break;
    case SliceMode::Crossline:
        values.reserve(g.n_xl);
        for (int32_t v : cube_->crosslines()) {
            values.append(v);
        }
        current_idx = xl_idx_;
        break;
    case SliceMode::Time:
        values.reserve(g.n_t);
        for (int t = 0; t < g.n_t; ++t) {
            values.append(cube_->timeMs(t));
        }
        current_idx = t_idx_;
        break;
    }

    slice_spin_->blockSignals(true);
    slice_spin_->setValues(values);
    slice_spin_->setCurrentIndex(current_idx);
    slice_spin_->blockSignals(false);
}

int MainWindow::maxSliceIndex() const {
    if (!cube_->isLoaded()) return 0;
    const auto& g = cube_->geometry();
    switch (mode_) {
    case SliceMode::Inline: return std::max(0, g.n_il - 1);
    case SliceMode::Crossline: return std::max(0, g.n_xl - 1);
    case SliceMode::Time: return std::max(0, g.n_t - 1);
    }
    return 0;
}

void MainWindow::updateClipRangeLabel() {
    if (!clip_range_label_ || !cube_->isLoaded()) {
        if (clip_range_label_) {
            clip_range_label_->setText(QStringLiteral("—"));
        }
        return;
    }
    float vmin = 0.f;
    float vmax = 0.f;
    cube_->clipRange(clip_percent_, vmin, vmax);
    clip_range_label_->setText(
        tr("[%1, %2]").arg(static_cast<double>(vmin), 0, 'g', 5).arg(static_cast<double>(vmax), 0, 'g', 5));
}

void MainWindow::refreshSlice() {
    if (!cube_->isLoaded() || !slice_view_) return;

    const auto& g = cube_->geometry();
    const ResampleParams resample = currentResampleParams();
    const CropBounds crop = fullCropBounds();
    std::vector<float> data;
    int w = 0, h = 0;
    std::vector<int32_t> horiz_labels;
    std::vector<int32_t> vert_labels;
    bool vert_is_time = false;
    float vert_step_ms = g.dt_ms;

    switch (mode_) {
    case SliceMode::Inline:
        data = cube_->readInlineSliceProcessed(il_idx_, crop, resample, w, h, horiz_labels, vert_labels);
        vert_is_time = true;
        vert_step_ms = resample.dt_out_ms > 0.f ? resample.dt_out_ms : g.dt_ms;
        break;
    case SliceMode::Crossline:
        data = cube_->readCrosslineSliceProcessed(xl_idx_, crop, resample, w, h, horiz_labels, vert_labels);
        vert_is_time = true;
        vert_step_ms = resample.dt_out_ms > 0.f ? resample.dt_out_ms : g.dt_ms;
        break;
    case SliceMode::Time:
        data = cube_->readTimeSliceProcessed(t_idx_, crop, resample, w, h, horiz_labels, vert_labels);
        vert_is_time = false;
        break;
    }

    applyCubeFftFilter(data, w, h, vert_step_ms);
    applyCubeFft2DFilter(data, w, h, horiz_labels, vert_labels);

    float vmin = 0.f;
    float vmax = 0.f;
    cube_->clipRange(clip_percent_, vmin, vmax);
    slice_view_->setAxisLabels(std::move(horiz_labels), std::move(vert_labels), vert_is_time, vert_step_ms);
    slice_view_->setColorMap(color_map_);
    slice_view_->setSlice(data, w, h, vmin, vmax);
    slice_w_ = w;
    slice_h_ = h;
    applyCropMask(w, h, horiz_labels, vert_labels, vert_is_time);
    updateClipRangeLabel();
    updateStatusBase();
}

CropBounds MainWindow::fullCropBounds() const {
    if (!cube_->isLoaded()) {
        return CropBounds{};
    }
    const auto& g = cube_->geometry();
    return CropBounds{0, std::max(0, g.n_il - 1), 0, std::max(0, g.n_xl - 1), 0,
                      std::max(0, g.n_t - 1)};
}

void MainWindow::resetCropBounds() {
    if (!cube_->isLoaded()) {
        return;
    }
    const auto& g = cube_->geometry();
    crop_il_min_ = 0;
    crop_il_max_ = std::max(0, g.n_il - 1);
    crop_xl_min_ = 0;
    crop_xl_max_ = std::max(0, g.n_xl - 1);
    crop_t_min_ = 0;
    crop_t_max_ = std::max(0, g.n_t - 1);
}

void MainWindow::updateCropSpinboxes() {
    if (!cube_->isLoaded()) {
        return;
    }

    const auto& g = cube_->geometry();
    QVector<int> il_values;
    QVector<int> xl_values;
    QVector<int> t_values;
    il_values.reserve(g.n_il);
    xl_values.reserve(g.n_xl);
    t_values.reserve(g.n_t);
    for (int32_t v : cube_->inlines()) {
        il_values.append(v);
    }
    for (int32_t v : cube_->crosslines()) {
        xl_values.append(v);
    }
    for (int t = 0; t < g.n_t; ++t) {
        t_values.append(cube_->timeMs(t));
    }

    const auto syncSpin = [](DiscreteValueSpinBox* spin, const QVector<int>& values, int idx,
                             bool enabled) {
        if (!spin) {
            return;
        }
        spin->blockSignals(true);
        spin->setEnabled(enabled);
        spin->setValues(values);
        spin->setCurrentIndex(idx);
        spin->blockSignals(false);
    };

    syncSpin(crop_il_min_spin_, il_values, crop_il_min_, true);
    syncSpin(crop_il_max_spin_, il_values, crop_il_max_, true);
    syncSpin(crop_xl_min_spin_, xl_values, crop_xl_min_, true);
    syncSpin(crop_xl_max_spin_, xl_values, crop_xl_max_, true);
    syncSpin(crop_t_min_spin_, t_values, crop_t_min_, true);
    syncSpin(crop_t_max_spin_, t_values, crop_t_max_, true);
}

void MainWindow::applyCropMask(int w, int h, const std::vector<int32_t>& horiz_labels,
                               const std::vector<int32_t>& vert_labels, bool vert_is_time) {
    if (!slice_view_ || !cube_->isLoaded() || w <= 0 || h <= 0) {
        return;
    }

    if (!crop_enabled_) {
        slice_view_->setCropRanges(0, w - 1, 0, h - 1, false);
        return;
    }

    int h_min = 0;
    int h_max = w - 1;
    int v_min = 0;
    int v_max = h - 1;
    bool mask_entire = false;

    switch (mode_) {
    case SliceMode::Inline:
        mask_entire = il_idx_ < crop_il_min_ || il_idx_ > crop_il_max_;
        h_min = crop_xl_min_;
        h_max = crop_xl_max_;
        timeCropRows(crop_t_min_, crop_t_max_, vert_labels, vert_is_time, cube_->timeMs(crop_t_min_),
                     cube_->timeMs(crop_t_max_), h, v_min, v_max);
        break;
    case SliceMode::Crossline:
        mask_entire = xl_idx_ < crop_xl_min_ || xl_idx_ > crop_xl_max_;
        h_min = crop_il_min_;
        h_max = crop_il_max_;
        timeCropRows(crop_t_min_, crop_t_max_, vert_labels, vert_is_time, cube_->timeMs(crop_t_min_),
                     cube_->timeMs(crop_t_max_), h, v_min, v_max);
        break;
    case SliceMode::Time:
        mask_entire = t_idx_ < crop_t_min_ || t_idx_ > crop_t_max_;
        if (horiz_labels.empty()) {
            h_min = crop_xl_min_;
            h_max = crop_xl_max_;
        } else {
            h_min = findFirstIndexAtOrAbove(horiz_labels, cube_->crosslineLabel(crop_xl_min_));
            h_max = findLastIndexAtOrBelow(horiz_labels, cube_->crosslineLabel(crop_xl_max_));
        }
        if (vert_labels.empty()) {
            v_min = crop_il_min_;
            v_max = crop_il_max_;
        } else {
            v_min = findFirstIndexAtOrAbove(vert_labels, cube_->inlineLabel(crop_il_min_));
            v_max = findLastIndexAtOrBelow(vert_labels, cube_->inlineLabel(crop_il_max_));
        }
        break;
    }

    h_min = std::clamp(h_min, 0, w - 1);
    h_max = std::clamp(h_max, 0, w - 1);
    v_min = std::clamp(v_min, 0, h - 1);
    v_max = std::clamp(v_max, 0, h - 1);
    if (h_min > h_max) {
        std::swap(h_min, h_max);
    }
    if (v_min > v_max) {
        std::swap(v_min, v_max);
    }

    slice_view_->setCropRanges(h_min, h_max, v_min, v_max, mask_entire);
}

CropBounds MainWindow::currentCropBounds() const {
    if (crop_enabled_ && cube_->isLoaded()) {
        return CropBounds{crop_il_min_, crop_il_max_, crop_xl_min_, crop_xl_max_, crop_t_min_,
                          crop_t_max_};
    }
    if (cube_->isLoaded()) {
        const auto& g = cube_->geometry();
        return CropBounds{0, std::max(0, g.n_il - 1), 0, std::max(0, g.n_xl - 1), 0,
                          std::max(0, g.n_t - 1)};
    }
    return CropBounds{};
}

ResampleParams MainWindow::currentResampleParams() const {
    if (!resample_enabled_) {
        return {};
    }
    ResampleParams params;
    if (resample_dt_spin_) {
        params.dt_out_ms = static_cast<float>(resample_dt_spin_->value());
    }
    if (resample_dil_spin_) {
        params.d_inline_out = static_cast<float>(resample_dil_spin_->value());
    }
    if (resample_dxl_spin_) {
        params.d_crossline_out = static_cast<float>(resample_dxl_spin_->value());
    }
    return params;
}

void MainWindow::resetResampleSpinboxes() {
    if (!cube_->isLoaded()) {
        return;
    }
    const NativeGridSteps steps = cube_->nativeGridSteps();
    if (resample_dt_spin_) {
        resample_dt_spin_->setValue(static_cast<double>(steps.dt_ms));
    }
    if (resample_dil_spin_) {
        resample_dil_spin_->setValue(static_cast<double>(steps.d_inline));
    }
    if (resample_dxl_spin_) {
        resample_dxl_spin_->setValue(static_cast<double>(steps.d_crossline));
    }
}

void MainWindow::updateResampleSpinboxes() {
    const bool enabled = cube_->isLoaded();
    if (resample_dt_spin_) {
        resample_dt_spin_->setEnabled(enabled);
    }
    if (resample_dil_spin_) {
        resample_dil_spin_->setEnabled(enabled);
    }
    if (resample_dxl_spin_) {
        resample_dxl_spin_->setEnabled(enabled);
    }
}

void MainWindow::onResampleChanged() {
    refreshSlice();
}

void MainWindow::onCropEnableToggled(bool enabled) {
    crop_enabled_ = enabled;
    refreshSlice();
}

void MainWindow::onResampleEnableToggled(bool enabled) {
    resample_enabled_ = enabled;
    refreshSlice();
}

void MainWindow::updateFilterToolState() {
    const bool loaded = cube_->isLoaded();
    const bool fft_mode = mode_ == SliceMode::Inline || mode_ == SliceMode::Crossline;
    const bool footprint_mode = mode_ == SliceMode::Time;

    if (btn_fft_select_) {
        if (!loaded || !fft_mode) {
            if (btn_fft_select_->isChecked()) {
                btn_fft_select_->setChecked(false);
            }
            btn_fft_select_->setEnabled(false);
        } else {
            btn_fft_select_->setEnabled(true);
        }
    }

    if (btn_footprint_select_) {
        if (!loaded || !footprint_mode) {
            if (btn_footprint_select_->isChecked()) {
                btn_footprint_select_->setChecked(false);
            }
            btn_footprint_select_->setEnabled(false);
        } else {
            btn_footprint_select_->setEnabled(true);
        }
    }

    if (fft_filter_enable_) {
        fft_filter_enable_->setEnabled(loaded && fft_mode && cube_fft_params_set_);
    }
    if (footprint_filter_enable_) {
        footprint_filter_enable_->setEnabled(loaded && footprint_mode && cube_footprint_params_set_);
    }

    const bool selection_active =
        (btn_fft_select_ && btn_fft_select_->isChecked()) ||
        (btn_footprint_select_ && btn_footprint_select_->isChecked());
    if (slice_view_ && (!loaded || !selection_active)) {
        slice_view_->setSelectionMode(false);
    }
}

void MainWindow::onFftSelectToggled(bool enabled) {
    if (enabled && btn_footprint_select_ && btn_footprint_select_->isChecked()) {
        btn_footprint_select_->blockSignals(true);
        btn_footprint_select_->setChecked(false);
        btn_footprint_select_->blockSignals(false);
    }
    if (slice_view_) {
        slice_view_->setSelectionMode(enabled);
    }
}

void MainWindow::onFootprintSelectToggled(bool enabled) {
    if (enabled && btn_fft_select_ && btn_fft_select_->isChecked()) {
        btn_fft_select_->blockSignals(true);
        btn_fft_select_->setChecked(false);
        btn_fft_select_->blockSignals(false);
    }
    if (slice_view_) {
        slice_view_->setSelectionMode(enabled);
    }
}

void MainWindow::onFftRegionSelected(int h0, int h1, int v0, int v1) {
    if (!cube_->isLoaded()) {
        return;
    }

    const ResampleParams resample = currentResampleParams();
    const CropBounds crop = fullCropBounds();
    std::vector<float> data;
    int w = 0;
    int h = 0;
    std::vector<int32_t> horiz_labels;
    std::vector<int32_t> vert_labels;

    switch (mode_) {
    case SliceMode::Inline:
        data = cube_->readInlineSliceProcessed(il_idx_, crop, resample, w, h, horiz_labels, vert_labels);
        break;
    case SliceMode::Crossline:
        data = cube_->readCrosslineSliceProcessed(xl_idx_, crop, resample, w, h, horiz_labels, vert_labels);
        break;
    case SliceMode::Time:
        data = cube_->readTimeSliceProcessed(t_idx_, crop, resample, w, h, horiz_labels, vert_labels);
        break;
    }

    if (data.empty() || w <= 0 || h <= 0) {
        return;
    }

    if (mode_ == SliceMode::Time) {
        if (!btn_footprint_select_ || !btn_footprint_select_->isChecked()) {
            return;
        }
        btn_footprint_select_->setChecked(false);
        if (slice_view_) {
            slice_view_->setSelectionMode(false);
        }

        const NativeGridSteps native = cube_->nativeGridSteps();
        double d_xl = uniformLabelStep(horiz_labels);
        double d_il = uniformLabelStep(vert_labels);
        if (d_xl <= 0.0) {
            d_xl = resample.d_crossline_out > 0.f ? resample.d_crossline_out : native.d_crossline;
        }
        if (d_il <= 0.0) {
            d_il = resample.d_inline_out > 0.f ? resample.d_inline_out : native.d_inline;
        }
        if (d_xl <= 0.0) {
            d_xl = 1.0;
        }
        if (d_il <= 0.0) {
            d_il = 1.0;
        }

        auto* dlg = new FftFilter2DDialog(data, w, h, h0, h1, v0, v1, d_xl, d_il, color_map_, clip_percent_,
                                          this);
        connect(dlg, &FftFilter2DDialog::applyToCubeRequested, this, &MainWindow::onFootprintApplyToCube);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        return;
    }

    if (!btn_fft_select_ || !btn_fft_select_->isChecked()) {
        return;
    }
    btn_fft_select_->setChecked(false);
    if (slice_view_) {
        slice_view_->setSelectionMode(false);
    }

    float dt_ms = cube_->geometry().dt_ms;
    if (resample.dt_out_ms > 0.f) {
        dt_ms = resample.dt_out_ms;
    }

    auto* dlg = new FftFilterDialog(data, w, h, h0, h1, v0, v1, dt_ms, mode_, this);
    connect(dlg, &FftFilterDialog::applyToCubeRequested, this, &MainWindow::onFftApplyToCube);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onFftApplyToCube(const FftFilterParams& params) {
    cube_fft_params_ = params;
    cube_fft_params_set_ = true;
    cube_fft_enabled_ = true;
    if (fft_filter_enable_) {
        fft_filter_enable_->blockSignals(true);
        fft_filter_enable_->setChecked(true);
        fft_filter_enable_->blockSignals(false);
    }
    updateFilterToolState();
    updateFftCubeFilterLabel();
    refreshSlice();
    statusBar()->showMessage(tr("Параметры FFT-фильтра сохранены"), 5000);
}

void MainWindow::onFootprintApplyToCube(const FftFilter2DParams& params) {
    cube_footprint_params_ = params;
    cube_footprint_params_set_ = true;
    cube_footprint_enabled_ = true;
    if (footprint_filter_enable_) {
        footprint_filter_enable_->blockSignals(true);
        footprint_filter_enable_->setChecked(true);
        footprint_filter_enable_->blockSignals(false);
    }
    updateFilterToolState();
    updateFootprintCubeFilterLabel();
    refreshSlice();
    statusBar()->showMessage(tr("Параметры footprint-фильтра сохранены"), 5000);
}

void MainWindow::onFftFilterEnableToggled(bool enabled) {
    cube_fft_enabled_ = enabled;
    updateFftCubeFilterLabel();
    refreshSlice();
}

void MainWindow::onFootprintFilterEnableToggled(bool enabled) {
    cube_footprint_enabled_ = enabled;
    updateFootprintCubeFilterLabel();
    refreshSlice();
}

void MainWindow::updateFftCubeFilterLabel() {
    if (!fft_cube_filter_label_) {
        return;
    }
    if (!cube_fft_params_set_) {
        fft_cube_filter_label_->setText(tr("Фильтр к кубу: нет"));
        return;
    }

    QString type_name;
    switch (cube_fft_params_.type) {
    case FftFilterType::Bandpass:
        type_name = tr("Bandpass");
        break;
    case FftFilterType::Lowpass:
        type_name = tr("Low-pass");
        break;
    case FftFilterType::Highpass:
        type_name = tr("High-pass");
        break;
    case FftFilterType::Notch:
        type_name = tr("Notch");
        break;
    }

    const QString state_note = cube_fft_enabled_ ? tr(" (вкл)") : tr(" (выкл)");
    if (cube_fft_params_.type == FftFilterType::Lowpass) {
        fft_cube_filter_label_->setText(
            tr("Фильтр к кубу: %1 ≤ %2 Hz, order %3%4")
                .arg(type_name)
                .arg(cube_fft_params_.f_high_hz, 0, 'f', 1)
                .arg(cube_fft_params_.order)
                .arg(state_note));
    } else if (cube_fft_params_.type == FftFilterType::Highpass) {
        fft_cube_filter_label_->setText(
            tr("Фильтр к кубу: %1 ≥ %2 Hz, order %3%4")
                .arg(type_name)
                .arg(cube_fft_params_.f_low_hz, 0, 'f', 1)
                .arg(cube_fft_params_.order)
                .arg(state_note));
    } else {
        fft_cube_filter_label_->setText(
            tr("Фильтр к кубу: %1 %2–%3 Hz, order %4%5")
                .arg(type_name)
                .arg(cube_fft_params_.f_low_hz, 0, 'f', 1)
                .arg(cube_fft_params_.f_high_hz, 0, 'f', 1)
                .arg(cube_fft_params_.order)
                .arg(state_note));
    }
}

void MainWindow::updateFootprintCubeFilterLabel() {
    if (!footprint_cube_filter_label_) {
        return;
    }
    if (!cube_footprint_params_set_) {
        footprint_cube_filter_label_->setText(tr("Фильтр к кубу: нет"));
        return;
    }

    QString type_name;
    switch (cube_footprint_params_.type) {
    case FftFilter2DType::FootprintIlXl:
        type_name = tr("Footprint IL-XL");
        break;
    case FftFilter2DType::FootprintIl:
        type_name = tr("Footprint IL");
        break;
    case FftFilter2DType::FootprintXl:
        type_name = tr("Footprint XL");
        break;
    }

    const QString state_note = cube_footprint_enabled_ ? tr(" (вкл)") : tr(" (выкл)");
    footprint_cube_filter_label_->setText(
        tr("Фильтр к кубу: %1, k_pass %2, k_cut IL %3, k_cut XL %4, k_smooth %5%6")
            .arg(type_name)
            .arg(cube_footprint_params_.k_pass, 0, 'f', 4)
            .arg(cube_footprint_params_.k_cut_il, 0, 'f', 4)
            .arg(cube_footprint_params_.k_cut_xl, 0, 'f', 4)
            .arg(cube_footprint_params_.k_smooth, 0, 'f', 4)
            .arg(state_note));
}

void MainWindow::applyCubeFftFilter(std::vector<float>& data, int w, int h, float dt_ms) {
    if (!cube_fft_params_set_ || !cube_fft_enabled_ || !cube_->isLoaded() || data.empty() ||
        w <= 0 || h <= 0) {
        return;
    }
    if (mode_ != SliceMode::Inline && mode_ != SliceMode::Crossline) {
        return;
    }
    data = filterSlice1D(data, w, h, dt_ms, cube_fft_params_);
}

void MainWindow::applyCubeFft2DFilter(std::vector<float>& data, int w, int h,
                                      const std::vector<int32_t>& horiz_labels,
                                      const std::vector<int32_t>& vert_labels) {
    if (!cube_footprint_params_set_ || !cube_footprint_enabled_ || !cube_->isLoaded() || data.empty() ||
        w <= 0 || h <= 0) {
        return;
    }
    if (mode_ != SliceMode::Time) {
        return;
    }

    const ResampleParams resample = currentResampleParams();
    const NativeGridSteps native = cube_->nativeGridSteps();
    double d_xl = uniformLabelStep(horiz_labels);
    double d_il = uniformLabelStep(vert_labels);
    if (d_xl <= 0.0) {
        d_xl = resample.d_crossline_out > 0.f ? resample.d_crossline_out : native.d_crossline;
    }
    if (d_il <= 0.0) {
        d_il = resample.d_inline_out > 0.f ? resample.d_inline_out : native.d_inline;
    }
    if (d_xl <= 0.0) {
        d_xl = 1.0;
    }
    if (d_il <= 0.0) {
        d_il = 1.0;
    }

    data = filterSlice2D(data, w, h, d_xl, d_il, cube_footprint_params_);
}

void MainWindow::onCropChanged() {
    if (!cube_->isLoaded()) {
        return;
    }

    crop_il_min_ = crop_il_min_spin_ ? crop_il_min_spin_->currentIndex() : crop_il_min_;
    crop_il_max_ = crop_il_max_spin_ ? crop_il_max_spin_->currentIndex() : crop_il_max_;
    crop_xl_min_ = crop_xl_min_spin_ ? crop_xl_min_spin_->currentIndex() : crop_xl_min_;
    crop_xl_max_ = crop_xl_max_spin_ ? crop_xl_max_spin_->currentIndex() : crop_xl_max_;
    crop_t_min_ = crop_t_min_spin_ ? crop_t_min_spin_->currentIndex() : crop_t_min_;
    crop_t_max_ = crop_t_max_spin_ ? crop_t_max_spin_->currentIndex() : crop_t_max_;

    if (crop_il_min_ > crop_il_max_) {
        crop_il_max_ = crop_il_min_;
        if (crop_il_max_spin_) {
            crop_il_max_spin_->blockSignals(true);
            crop_il_max_spin_->setCurrentIndex(crop_il_max_);
            crop_il_max_spin_->blockSignals(false);
        }
    }
    if (crop_xl_min_ > crop_xl_max_) {
        crop_xl_max_ = crop_xl_min_;
        if (crop_xl_max_spin_) {
            crop_xl_max_spin_->blockSignals(true);
            crop_xl_max_spin_->setCurrentIndex(crop_xl_max_);
            crop_xl_max_spin_->blockSignals(false);
        }
    }
    if (crop_t_min_ > crop_t_max_) {
        crop_t_max_ = crop_t_min_;
        if (crop_t_max_spin_) {
            crop_t_max_spin_->blockSignals(true);
            crop_t_max_spin_->setCurrentIndex(crop_t_max_);
            crop_t_max_spin_->blockSignals(false);
        }
    }

    refreshSlice();
}

void MainWindow::updateStatusBase() {
    if (!cube_->isLoaded()) {
        base_status_ = tr("Нет данных");
        if (status_label_) status_label_->setText(base_status_);
        return;
    }
    const auto& g = cube_->geometry();
    QString modeStr;
    int sliceLabel = 0;
    switch (mode_) {
    case SliceMode::Inline:
        modeStr = tr("Inline");
        sliceLabel = cube_->inlineLabel(il_idx_);
        break;
    case SliceMode::Crossline:
        modeStr = tr("Crossline");
        sliceLabel = cube_->crosslineLabel(xl_idx_);
        break;
    case SliceMode::Time:
        modeStr = tr("Time");
        sliceLabel = cube_->timeMs(t_idx_);
        break;
    }
    base_status_ = tr("%1 %2 | IL×XL×T = %3×%4×%5 | dt %6 ms")
                       .arg(modeStr)
                       .arg(sliceLabel)
                       .arg(g.n_il)
                       .arg(g.n_xl)
                       .arg(g.n_t)
                       .arg(g.dt_ms, 0, 'g', 4);
    if (status_label_) status_label_->setText(base_status_);
}

void MainWindow::onSliceScroll(int delta) {
    setCurrentSliceIndex(currentSliceIndex() + delta);
    slice_scroll_->blockSignals(true);
    slice_spin_->blockSignals(true);
    slice_scroll_->setValue(currentSliceIndex());
    slice_spin_->setCurrentIndex(currentSliceIndex());
    slice_scroll_->blockSignals(false);
    slice_spin_->blockSignals(false);
    refreshSlice();
}

void MainWindow::onSliceSlider(int value) {
    setCurrentSliceIndex(value);
    slice_scroll_->blockSignals(true);
    slice_spin_->blockSignals(true);
    slice_scroll_->setValue(value);
    slice_spin_->setCurrentIndex(value);
    slice_scroll_->blockSignals(false);
    slice_spin_->blockSignals(false);
    refreshSlice();
}

void MainWindow::onNavigatorInline(int il_idx) {
    il_idx_ = il_idx;
    if (mode_ != SliceMode::Inline) setSliceMode(SliceMode::Inline);
    onSliceSlider(il_idx);
}

void MainWindow::onNavigatorCrossline(int xl_idx) {
    xl_idx_ = xl_idx;
    if (mode_ != SliceMode::Crossline) setSliceMode(SliceMode::Crossline);
    onSliceSlider(xl_idx);
}

void MainWindow::onNavigatorTime(int t_idx) {
    t_idx_ = t_idx;
    if (mode_ != SliceMode::Time) setSliceMode(SliceMode::Time);
    onSliceSlider(t_idx);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QString path = urls.first().toLocalFile();
    if (!path.isEmpty()) {
        loadSegy(path);
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_I: setSliceMode(SliceMode::Inline); break;
    case Qt::Key_X: setSliceMode(SliceMode::Crossline); break;
    case Qt::Key_T: setSliceMode(SliceMode::Time); break;
    default: break;
    }
    QMainWindow::keyPressEvent(event);
}

}  // namespace kubik
