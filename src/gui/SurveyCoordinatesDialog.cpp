#include "SurveyCoordinatesDialog.hpp"
#include "SurveyMapWidget.hpp"

#include "kubik/SegyCube.hpp"

#include <QAbstractItemView>
#include <QClipboard>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString formatStat(const kubik::MinMaxMedian& stat) {
    return QObject::tr("%1 / %2 / %3")
        .arg(stat.min_val, 0, 'f', 2)
        .arg(stat.max_val, 0, 'f', 2)
        .arg(stat.median_val, 0, 'f', 2);
}

QString tableCellText(const QTableWidget* table, int row, int col) {
    const QTableWidgetItem* item = table->item(row, col);
    return item ? item->text() : QString();
}

QString copyTableRows(const QTableWidget* table, int top_row, int bottom_row, int left_col, int right_col) {
    QString text;
    for (int row = top_row; row <= bottom_row; ++row) {
        QStringList cells;
        for (int col = left_col; col <= right_col; ++col) {
            cells << tableCellText(table, row, col);
        }
        text += cells.join(QLatin1Char('\t'));
        if (row < bottom_row) {
            text += QLatin1Char('\n');
        }
    }
    return text;
}

void copyTableToClipboard(const QTableWidget* table, bool include_headers) {
    if (!table || table->columnCount() == 0) {
        return;
    }

    QString text;
    const QList<QTableWidgetSelectionRange> ranges = table->selectedRanges();
    if (ranges.isEmpty()) {
        if (include_headers) {
            QStringList headers;
            for (int col = 0; col < table->columnCount(); ++col) {
                const QTableWidgetItem* header = table->horizontalHeaderItem(col);
                headers << (header ? header->text() : QString());
            }
            text += headers.join(QLatin1Char('\t'));
            if (table->rowCount() > 0) {
                text += QLatin1Char('\n');
            }
        }
        if (table->rowCount() > 0) {
            text += copyTableRows(table, 0, table->rowCount() - 1, 0, table->columnCount() - 1);
        }
    } else {
        for (int i = 0; i < ranges.size(); ++i) {
            const QTableWidgetSelectionRange& range = ranges.at(i);
            text += copyTableRows(table, range.topRow(), range.bottomRow(), range.leftColumn(),
                                  range.rightColumn());
            if (i + 1 < ranges.size()) {
                text += QLatin1Char('\n');
            }
        }
    }

    if (!text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
    }
}

void enableTableCopy(QTableWidget* table) {
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    auto copy = [table]() { copyTableToClipboard(table, /*include_headers=*/true); };

    auto* copyShortcut = new QShortcut(QKeySequence::Copy, table);
    QObject::connect(copyShortcut, &QShortcut::activated, table, copy);

    QObject::connect(table, &QTableWidget::customContextMenuRequested, table,
                     [table, copy](const QPoint& pos) {
                         QMenu menu(table);
                         QAction* copyAct = menu.addAction(QObject::tr("Копировать"));
                         QObject::connect(copyAct, &QAction::triggered, table, copy);
                         menu.exec(table->viewport()->mapToGlobal(pos));
                     });
}

QTableWidget* makeReadOnlyTable(QWidget* parent) {
    auto* table = new QTableWidget(parent);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    enableTableCopy(table);
    return table;
}

}  // namespace

namespace kubik {

SurveyCoordinatesDialog::SurveyCoordinatesDialog(const SegyCube& cube, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Координаты"));
    setMaximumSize(920, 880);
    setupUi(cube);
}

void SurveyCoordinatesDialog::setupUi(const SegyCube& cube) {
    const SurveyCoordinateStats stats = cube.surveyCoordinates();

    auto* layout = new QVBoxLayout(this);

    auto* statsBox = new QGroupBox(tr("Статистика"), this);
    auto* form = new QFormLayout(statsBox);
    form->addRow(tr("Inline (min / max / median)"), new QLabel(formatStat(stats.inline_stats), statsBox));
    form->addRow(tr("Crossline (min / max / median)"), new QLabel(formatStat(stats.crossline_stats), statsBox));
    form->addRow(tr("CDP_X (min / max / median)"), new QLabel(formatStat(stats.cdp_x_stats), statsBox));
    form->addRow(tr("CDP_Y (min / max / median)"), new QLabel(formatStat(stats.cdp_y_stats), statsBox));
    form->addRow(tr("Inline Azimuth (° от оси Y)"),
                 new QLabel(QString::number(stats.inline_azimuth_deg, 'f', 2), statsBox));
    layout->addWidget(statsBox);

    auto* mapBox = new QGroupBox(tr("Карта съёмки"), this);
    auto* mapLayout = new QVBoxLayout(mapBox);
    auto* mapWidget = new SurveyMapWidget(mapBox);
    mapWidget->setSurveyData(stats);
    mapLayout->addWidget(mapWidget, 0, Qt::AlignHCenter);
    layout->addWidget(mapBox, 0);

    auto* cornersBox = new QGroupBox(tr("Угловые точки съёмки"), this);
    auto* cornersLayout = new QVBoxLayout(cornersBox);
    auto* cornersTable = makeReadOnlyTable(cornersBox);
    cornersTable->setColumnCount(5);
    cornersTable->setHorizontalHeaderLabels(
        {tr("Номер точки"), tr("Inline"), tr("Crossline"), tr("CDP_X"), tr("CDP_Y")});
    cornersTable->setRowCount(static_cast<int>(stats.corners.size()));
    for (int row = 0; row < cornersTable->rowCount(); ++row) {
        const SurveyCornerPoint& pt = stats.corners[static_cast<std::size_t>(row)];
        cornersTable->setItem(row, 0, new QTableWidgetItem(QString::number(pt.point_num)));
        cornersTable->setItem(row, 1, new QTableWidgetItem(QString::number(pt.inline_label)));
        cornersTable->setItem(row, 2, new QTableWidgetItem(QString::number(pt.crossline_label)));
        cornersTable->setItem(row, 3, new QTableWidgetItem(QString::number(pt.cdp_x, 'f', 2)));
        cornersTable->setItem(row, 4, new QTableWidgetItem(QString::number(pt.cdp_y, 'f', 2)));
    }
    cornersTable->resizeColumnsToContents();
    cornersLayout->addWidget(cornersTable);
    layout->addWidget(cornersBox);

    layout->setSizeConstraint(QLayout::SetFixedSize);
}

}  // namespace kubik
