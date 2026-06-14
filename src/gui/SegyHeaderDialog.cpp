#include "SegyHeaderDialog.hpp"

#include "kubik/SegyCube.hpp"

#include <QAbstractItemView>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <segyio/segy.h>

#include <algorithm>
#include <cstring>

namespace {

struct FieldDesc {
    int offset;
    const char* name;
};

const FieldDesc kBinFields[] = {
    {SEGY_BIN_JOB_ID, "Job ID"},
    {SEGY_BIN_LINE_NUMBER, "Line number"},
    {SEGY_BIN_REEL_NUMBER, "Reel number"},
    {SEGY_BIN_ENSEMBLE_TRACES, "Ensemble traces"},
    {SEGY_BIN_AUX_ENSEMBLE_TRACES, "Aux ensemble traces"},
    {SEGY_BIN_INTERVAL, "Sample interval (µs)"},
    {SEGY_BIN_INTERVAL_ORIG, "Sample interval original (µs)"},
    {SEGY_BIN_SAMPLES, "Samples per trace"},
    {SEGY_BIN_SAMPLES_ORIG, "Samples per trace original"},
    {SEGY_BIN_FORMAT, "Data sample format"},
    {SEGY_BIN_ENSEMBLE_FOLD, "Ensemble fold"},
    {SEGY_BIN_SORTING_CODE, "Sorting code"},
    {SEGY_BIN_VERTICAL_SUM, "Vertical sum"},
    {SEGY_BIN_SWEEP_FREQ_START, "Sweep frequency start"},
    {SEGY_BIN_SWEEP_FREQ_END, "Sweep frequency end"},
    {SEGY_BIN_SWEEP_LENGTH, "Sweep length"},
    {SEGY_BIN_SWEEP, "Sweep"},
    {SEGY_BIN_SWEEP_CHANNEL, "Sweep channel"},
    {SEGY_BIN_SWEEP_TAPER_START, "Sweep taper start"},
    {SEGY_BIN_SWEEP_TAPER_END, "Sweep taper end"},
    {SEGY_BIN_TAPER, "Taper"},
    {SEGY_BIN_CORRELATED_TRACES, "Correlated traces"},
    {SEGY_BIN_BIN_GAIN_RECOVERY, "Binary gain recovery"},
    {SEGY_BIN_AMPLITUDE_RECOVERY, "Amplitude recovery"},
    {SEGY_BIN_MEASUREMENT_SYSTEM, "Measurement system"},
    {SEGY_BIN_IMPULSE_POLARITY, "Impulse polarity"},
    {SEGY_BIN_VIBRATORY_POLARITY, "Vibratory polarity"},
    {SEGY_BIN_EXT_ENSEMBLE_TRACES, "Ext ensemble traces"},
    {SEGY_BIN_EXT_AUX_ENSEMBLE_TRACES, "Ext aux ensemble traces"},
    {SEGY_BIN_EXT_SAMPLES, "Ext samples per trace"},
    {SEGY_BIN_EXT_INTERVAL, "Ext sample interval"},
    {SEGY_BIN_EXT_INTERVAL_ORIG, "Ext sample interval original"},
    {SEGY_BIN_EXT_SAMPLES_ORIG, "Ext samples per trace original"},
    {SEGY_BIN_EXT_ENSEMBLE_FOLD, "Ext ensemble fold"},
    {SEGY_BIN_INTEGER_CONSTANT, "Integer constant"},
    {SEGY_BIN_SEGY_REVISION, "SEG-Y revision"},
    {SEGY_BIN_SEGY_REVISION_MINOR, "SEG-Y revision minor"},
    {SEGY_BIN_TRACE_FLAG, "Trace flag"},
    {SEGY_BIN_EXT_HEADERS, "Extended textual headers"},
    {SEGY_BIN_MAX_ADDITIONAL_TR_HEADERS, "Max additional trace headers"},
    {SEGY_BIN_SURVEY_TYPE, "Survey type"},
    {SEGY_BIN_TIME_BASIS_CODE, "Time basis code"},
    {SEGY_BIN_NR_TRACES_IN_STREAM, "Nr traces in stream"},
    {SEGY_BIN_FIRST_TRACE_OFFSET, "First trace offset"},
    {SEGY_BIN_NR_TRAILER_RECORDS, "Nr trailer records"},
};

const FieldDesc kTraceFields[] = {
    {SEGY_TR_SEQ_LINE, "TRACE_SEQUENCE_LINE"},
    {SEGY_TR_SEQ_FILE, "TRACE_SEQUENCE_FILE"},
    {SEGY_TR_FIELD_RECORD, "FieldRecord"},
    {SEGY_TR_NUMBER_ORIG_FIELD, "TraceNumber"},
    {SEGY_TR_ENERGY_SOURCE_POINT, "EnergySourcePoint"},
    {SEGY_TR_ENSEMBLE, "CDP"},
    {SEGY_TR_NUM_IN_ENSEMBLE, "CDP_TRACE"},
    {SEGY_TR_TRACE_ID, "TraceIdentificationCode"},
    {SEGY_TR_SUMMED_TRACES, "NSummedTraces"},
    {SEGY_TR_STACKED_TRACES, "NStackedTraces"},
    {SEGY_TR_DATA_USE, "DataUse"},
    {SEGY_TR_OFFSET, "offset"},
    {SEGY_TR_RECV_GROUP_ELEV, "ReceiverGroupElevation"},
    {SEGY_TR_SOURCE_SURF_ELEV, "SourceSurfaceElevation"},
    {SEGY_TR_SOURCE_DEPTH, "SourceDepth"},
    {SEGY_TR_RECV_DATUM_ELEV, "ReceiverDatumElevation"},
    {SEGY_TR_SOURCE_DATUM_ELEV, "SourceDatumElevation"},
    {SEGY_TR_SOURCE_WATER_DEPTH, "SourceWaterDepth"},
    {SEGY_TR_GROUP_WATER_DEPTH, "GroupWaterDepth"},
    {SEGY_TR_ELEV_SCALAR, "ElevationScalar"},
    {SEGY_TR_SOURCE_GROUP_SCALAR, "SourceGroupScalar"},
    {SEGY_TR_SOURCE_X, "SourceX"},
    {SEGY_TR_SOURCE_Y, "SourceY"},
    {SEGY_TR_GROUP_X, "GroupX"},
    {SEGY_TR_GROUP_Y, "GroupY"},
    {SEGY_TR_COORD_UNITS, "CoordinateUnits"},
    {SEGY_TR_WEATHERING_VELO, "WeatheringVelocity"},
    {SEGY_TR_SUBWEATHERING_VELO, "SubWeatheringVelocity"},
    {SEGY_TR_SOURCE_UPHOLE_TIME, "SourceUpholeTime"},
    {SEGY_TR_GROUP_UPHOLE_TIME, "GroupUpholeTime"},
    {SEGY_TR_SOURCE_STATIC_CORR, "SourceStaticCorrection"},
    {SEGY_TR_GROUP_STATIC_CORR, "GroupStaticCorrection"},
    {SEGY_TR_TOT_STATIC_APPLIED, "TotalStaticApplied"},
    {SEGY_TR_LAG_A, "LagTimeA"},
    {SEGY_TR_LAG_B, "LagTimeB"},
    {SEGY_TR_DELAY_REC_TIME, "DelayRecordingTime"},
    {SEGY_TR_MUTE_TIME_START, "MuteTimeStart"},
    {SEGY_TR_MUTE_TIME_END, "MuteTimeEND"},
    {SEGY_TR_SAMPLE_COUNT, "TRACE_SAMPLE_COUNT"},
    {SEGY_TR_SAMPLE_INTER, "TRACE_SAMPLE_INTERVAL"},
    {SEGY_TR_GAIN_TYPE, "GainType"},
    {SEGY_TR_INSTR_GAIN_CONST, "InstrumentGainConstant"},
    {SEGY_TR_INSTR_INIT_GAIN, "InstrumentInitialGain"},
    {SEGY_TR_CORRELATED, "Correlated"},
    {SEGY_TR_SWEEP_FREQ_START, "SweepFrequencyStart"},
    {SEGY_TR_SWEEP_FREQ_END, "SweepFrequencyEnd"},
    {SEGY_TR_SWEEP_LENGTH, "SweepLength"},
    {SEGY_TR_SWEEP_TYPE, "SweepType"},
    {SEGY_TR_SWEEP_TAPERLEN_START, "SweepTraceTaperLengthStart"},
    {SEGY_TR_SWEEP_TAPERLEN_END, "SweepTraceTaperLengthEnd"},
    {SEGY_TR_TAPER_TYPE, "TaperType"},
    {SEGY_TR_ALIAS_FILT_FREQ, "AliasFilterFrequency"},
    {SEGY_TR_ALIAS_FILT_SLOPE, "AliasFilterSlope"},
    {SEGY_TR_NOTCH_FILT_FREQ, "NotchFilterFrequency"},
    {SEGY_TR_NOTCH_FILT_SLOPE, "NotchFilterSlope"},
    {SEGY_TR_LOW_CUT_FREQ, "LowCutFrequency"},
    {SEGY_TR_HIGH_CUT_FREQ, "HighCutFrequency"},
    {SEGY_TR_LOW_CUT_SLOPE, "LowCutSlope"},
    {SEGY_TR_HIGH_CUT_SLOPE, "HighCutSlope"},
    {SEGY_TR_YEAR_DATA_REC, "YearDataRecorded"},
    {SEGY_TR_DAY_OF_YEAR, "DayOfYear"},
    {SEGY_TR_HOUR_OF_DAY, "HourOfDay"},
    {SEGY_TR_MIN_OF_HOUR, "MinuteOfHour"},
    {SEGY_TR_SEC_OF_MIN, "SecondOfMinute"},
    {SEGY_TR_TIME_BASE_CODE, "TimeBaseCode"},
    {SEGY_TR_WEIGHTING_FAC, "TraceWeightingFactor"},
    {SEGY_TR_GEOPHONE_GROUP_ROLL1, "GeophoneGroupNumberRoll1"},
    {SEGY_TR_GEOPHONE_GROUP_FIRST, "GeophoneGroupNumberFirstTraceOrigField"},
    {SEGY_TR_GEOPHONE_GROUP_LAST, "GeophoneGroupNumberLastTraceOrigField"},
    {SEGY_TR_GAP_SIZE, "GapSize"},
    {SEGY_TR_OVER_TRAVEL, "OverTravel"},
    {SEGY_TR_CDP_X, "CDP_X"},
    {SEGY_TR_CDP_Y, "CDP_Y"},
    {SEGY_TR_INLINE, "INLINE_3D"},
    {SEGY_TR_CROSSLINE, "CROSSLINE_3D"},
    {SEGY_TR_SHOT_POINT, "ShotPoint"},
    {SEGY_TR_SHOT_POINT_SCALAR, "ShotPointScalar"},
    {SEGY_TR_MEASURE_UNIT, "TraceValueMeasurementUnit"},
    {SEGY_TR_TRANSDUCTION_MANT, "TransductionConstantMantissa"},
    {SEGY_TR_TRANSDUCTION_EXP, "TransductionConstantPower"},
    {SEGY_TR_TRANSDUCTION_UNIT, "TransductionUnit"},
    {SEGY_TR_DEVICE_ID, "TraceIdentifier"},
    {SEGY_TR_SCALAR_TRACE_HEADER, "ScalarTraceHeader"},
    {SEGY_TR_SOURCE_TYPE, "SourceType"},
    {SEGY_TR_SOURCE_ENERGY_DIR_VERT, "SourceEnergyDirectionVert"},
    {SEGY_TR_SOURCE_ENERGY_DIR_XLINE, "SourceEnergyDirectionXline"},
    {SEGY_TR_SOURCE_ENERGY_DIR_ILINE, "SourceEnergyDirectionIline"},
    {SEGY_TR_SOURCE_MEASURE_MANT, "SourceMeasurementMantissa"},
    {SEGY_TR_SOURCE_MEASURE_EXP, "SourceMeasurementExponent"},
    {SEGY_TR_SOURCE_MEASURE_UNIT, "SourceMeasurementUnit"},
    {SEGY_TR_UNASSIGNED1, "UnassignedInt1"},
    {SEGY_TR_UNASSIGNED2, "UnassignedInt2"},
};

QString formatFieldData(const segy_field_data& fd) {
    switch (static_cast<SEGY_ENTRY_TYPE>(fd.entry_type)) {
    case SEGY_ENTRY_TYPE_INT2:
        return QString::number(fd.value.i16);
    case SEGY_ENTRY_TYPE_INT4:
        return QString::number(fd.value.i32);
    case SEGY_ENTRY_TYPE_INT8:
        return QString::number(fd.value.i64);
    case SEGY_ENTRY_TYPE_UINT1:
        return QString::number(fd.value.u8);
    case SEGY_ENTRY_TYPE_UINT2:
        return QString::number(fd.value.u16);
    case SEGY_ENTRY_TYPE_UINT4:
        return QString::number(fd.value.u32);
    case SEGY_ENTRY_TYPE_UINT8:
        return QString::number(fd.value.u64);
    case SEGY_ENTRY_TYPE_IBMFP:
    case SEGY_ENTRY_TYPE_IEEE32:
        return QString::number(fd.value.f32, 'g', 8);
    case SEGY_ENTRY_TYPE_IEEE64:
        return QString::number(fd.value.f64, 'g', 10);
    case SEGY_ENTRY_TYPE_LINETRC:
    case SEGY_ENTRY_TYPE_REELTRC:
    case SEGY_ENTRY_TYPE_LINETRC8:
    case SEGY_ENTRY_TYPE_REELTRC8:
    case SEGY_ENTRY_TYPE_COOR4:
    case SEGY_ENTRY_TYPE_ELEV4:
    case SEGY_ENTRY_TYPE_TIME2:
    case SEGY_ENTRY_TYPE_SPNUM4:
    case SEGY_ENTRY_TYPE_SCALE6_MANT:
    case SEGY_ENTRY_TYPE_SCALE6_EXP:
        return QString::number(fd.value.i32);
    case SEGY_ENTRY_TYPE_STRING8: {
        char s[9] = {};
        std::memcpy(s, fd.value.str8, 8);
        return QString::fromLatin1(s).trimmed();
    }
    default:
        return QStringLiteral("—");
    }
}

QString formatBinValue(int field, const segy_field_data& fd) {
    if (field == SEGY_BIN_FORMAT) {
        return QStringLiteral("%1 (%2)")
            .arg(fd.value.i16)
            .arg(QString::fromUtf8(kubik::segySampleFormatName(fd.value.i16)));
    }
    return formatFieldData(fd);
}

QTableWidget* makeReadOnlyTable(QWidget* parent) {
    auto* table = new QTableWidget(parent);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    return table;
}

}  // namespace

namespace kubik {

SegyHeaderDialog::SegyHeaderDialog(const SegyCube& cube, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Заголовки SEG-Y"));
    resize(900, 560);
    setupUi(cube);
}

void SegyHeaderDialog::setupUi(const SegyCube& cube) {
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    const auto& h = cube.headers();

    auto* textual = new QTextEdit(tabs);
    textual->setReadOnly(true);
    textual->setFontFamily(QStringLiteral("Monospace"));
    QString text;
    for (int i = 0; i < 40; ++i) {
        char line[81] = {};
        std::memcpy(line, h.textual + i * 80, 80);
        line[80] = '\0';
        text += QString::fromLatin1(line).trimmed() + QLatin1Char('\n');
    }
    textual->setPlainText(text);
    tabs->addTab(textual, tr("Textual Header"));

    auto* binary = makeReadOnlyTable(tabs);
    binary->setColumnCount(2);
    binary->setHorizontalHeaderLabels({tr("Название заголовка"), tr("Значение")});
    binary->setRowCount(static_cast<int>(sizeof(kBinFields) / sizeof(kBinFields[0])));
    for (int row = 0; row < binary->rowCount(); ++row) {
        const FieldDesc& desc = kBinFields[static_cast<std::size_t>(row)];
        segy_field_data fd{};
        QString value = QStringLiteral("—");
        if (segy_get_binfield(h.binary, desc.offset, &fd) == SEGY_OK) {
            value = formatBinValue(desc.offset, fd);
        } else {
            int iv = 0;
            if (segy_get_binfield_int(h.binary, desc.offset, &iv) == SEGY_OK) {
                value = QString::number(iv);
            }
        }
        binary->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(desc.name)));
        binary->setItem(row, 1, new QTableWidgetItem(value));
    }
    binary->resizeColumnsToContents();
    tabs->addTab(binary, tr("Binary Header"));

    auto* traceTab = new QWidget(tabs);
    auto* traceLayout = new QVBoxLayout(traceTab);
    auto* traceNote = new QLabel(traceTab);
    auto* traceTable = makeReadOnlyTable(traceTab);

    const int n_fields = static_cast<int>(sizeof(kTraceFields) / sizeof(kTraceFields[0]));
    traceTable->setColumnCount(n_fields + 1);
    QStringList headers;
    headers << tr("Trace");
    for (int i = 0; i < n_fields; ++i) {
        headers << QString::fromUtf8(kTraceFields[static_cast<std::size_t>(i)].name);
    }
    traceTable->setHorizontalHeaderLabels(headers);

    segy_file* fp = segy_open(cube.path().c_str(), "rb");
    int n_traces = 0;
    if (fp && segy_collect_metadata(fp, -1, -1, -1) == SEGY_OK) {
        n_traces = fp->metadata.tracecount;
    }

    constexpr int kMaxTraces = 5000;
    const int rows = n_traces > 0 ? std::min(n_traces, kMaxTraces) : 0;
    traceTable->setRowCount(rows);

    if (n_traces > kMaxTraces) {
        traceNote->setText(tr("Показаны первые %1 из %2 трасс.").arg(kMaxTraces).arg(n_traces));
    } else if (n_traces > 0) {
        traceNote->setText(tr("Трасс: %1").arg(n_traces));
    } else {
        traceNote->setText(tr("Не удалось прочитать trace headers."));
    }

    if (fp && rows > 0) {
        const segy_entry_definition* tr_map = segy_traceheader_default_map();
        char hdr[SEGY_TRACE_HEADER_SIZE];
        for (int tr = 0; tr < rows; ++tr) {
            traceTable->setItem(tr, 0, new QTableWidgetItem(QString::number(tr)));
            if (segy_read_standard_traceheader(fp, tr, hdr) != SEGY_OK) {
                continue;
            }
            for (int col = 0; col < n_fields; ++col) {
                const FieldDesc& desc = kTraceFields[static_cast<std::size_t>(col)];
                segy_field_data fd{};
                QString value = QStringLiteral("—");
                if (segy_get_tracefield(hdr, tr_map, desc.offset, &fd) == SEGY_OK) {
                    value = formatFieldData(fd);
                } else {
                    int iv = 0;
                    if (segy_get_tracefield_int(hdr, desc.offset, &iv) == SEGY_OK) {
                        value = QString::number(iv);
                    }
                }
                traceTable->setItem(tr, col + 1, new QTableWidgetItem(value));
            }
        }
    }
    if (fp) {
        segy_close(fp);
    }

    traceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    traceLayout->addWidget(traceNote);
    traceLayout->addWidget(traceTable, 1);
    tabs->addTab(traceTab, tr("Trace Header"));

    layout->addWidget(tabs);
}

}  // namespace kubik
