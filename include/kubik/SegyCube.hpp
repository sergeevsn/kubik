#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "kubik/FftFilter.hpp"
#include "kubik/Resample.hpp"
#include "kubik/types.hpp"

namespace kubik {

struct CubeGeometry {
    int n_il = 0;
    int n_xl = 0;
    int n_t = 0;
    int32_t min_il = 0;
    int32_t max_il = 0;
    int32_t min_xl = 0;
    int32_t max_xl = 0;
    float dt_ms = 0.f;
    /// SEGY_BIN_FORMAT из binary header (1=IBM float, 5=IEEE float, …).
    int sample_format = 0;
};

const char* segySampleFormatName(int format_code);

struct CropBounds {
    int il_min = 0;
    int il_max = 0;
    int xl_min = 0;
    int xl_max = 0;
    int t_min = 0;
    int t_max = 0;
};

struct MinMaxMedian {
    double min_val = 0.0;
    double max_val = 0.0;
    double median_val = 0.0;
};

struct SurveyCdpPoint {
    double cdp_x = 0.0;
    double cdp_y = 0.0;
};

struct SurveyCornerPoint {
    int point_num = 0;
    int32_t inline_label = 0;
    int32_t crossline_label = 0;
    double cdp_x = 0.0;
    double cdp_y = 0.0;
};

struct SurveyCoordinateStats {
    MinMaxMedian inline_stats;
    MinMaxMedian crossline_stats;
    MinMaxMedian cdp_x_stats;
    MinMaxMedian cdp_y_stats;
    /// Азимут направления inline (градусы от оси Y, по часовой).
    double inline_azimuth_deg = 0.0;
    std::vector<SurveyCdpPoint> cdp_points;
    std::vector<SurveyCornerPoint> corners;
};

struct SaveCroppedProgress {
    enum class Stage {
        Prepare,
        ReadTraces,
        SpatialResample,
        Fft1D,
        Fft2D,
        WriteSegy,
    };

    Stage stage = Stage::Prepare;
    int stage_current = 0;
    int stage_total = 1;
    int overall_current = 0;
    int overall_total = 1;
};

using SaveCroppedProgressCallback = std::function<bool(const SaveCroppedProgress&)>;

class SaveCanceled : public std::runtime_error {
public:
    SaveCanceled() : std::runtime_error("save canceled") {}
};

/// Режим загрузки куба.
enum class CubeLoadMode {
    /// Срезы читаются с диска; clip по центральному inline (быстрое открытие).
    Lazy,
    /// Весь куб амплитуд в RAM; быстрые срезы, в т.ч. Time.
    InMemory,
};

struct CubeLoadProgress {
    enum class Stage {
        ScanHeaders,
        LoadVolume,
        BuildStats,
    };

    Stage stage = Stage::ScanHeaders;
    int current = 0;
    int total = 1;
};

using CubeLoadProgressCallback = std::function<bool(const CubeLoadProgress&)>;

struct CubeLoadOptions {
    CubeLoadMode mode = CubeLoadMode::Lazy;
    int inline_field = SEGY_TR_INLINE;
    int crossline_field = SEGY_TR_CROSSLINE;
    CubeLoadProgressCallback progress;
};

class LoadCanceled : public std::runtime_error {
public:
    LoadCanceled() : std::runtime_error("load canceled") {}
};

/// 3D post-stack куб из SEG-Y: скан заголовков, индекс IL×XL → trace id, чтение срезов.
class SegyCube {
public:
    SegyCube() = default;
    ~SegyCube();

    SegyCube(const SegyCube&) = delete;
    SegyCube& operator=(const SegyCube&) = delete;

    void load(const std::string& path, const CubeLoadOptions& options = {});
    void close();
    bool isLoaded() const { return loaded_; }
    CubeLoadMode loadMode() const { return load_mode_; }
    bool isInMemory() const { return !volume_.empty(); }

    const std::string& path() const { return path_; }
    const CubeGeometry& geometry() const { return geom_; }
    const FileHeaders& headers() const { return headers_; }

    const std::vector<int32_t>& inlines() const { return inlines_; }
    const std::vector<int32_t>& crosslines() const { return crosslines_; }

    int32_t inlineLabel(int il_idx) const;
    int32_t crosslineLabel(int xl_idx) const;
    int inlineIndex(int32_t il) const;
    int crosslineIndex(int32_t xl) const;
    int timeMs(int t_idx) const;

    /// Срез inline: ширина = n_xl (crossline), высота = n_t (время).
    std::vector<float> readInlineSlice(int il_idx) const;
    /// Срез crossline: ширина = n_il, высота = n_t.
    std::vector<float> readCrosslineSlice(int xl_idx) const;
    /// Time slice: ширина = n_xl, высота = n_il.
    std::vector<float> readTimeSlice(int t_idx) const;

    NativeGridSteps nativeGridSteps() const;

    /// Срез после кропа и ресемплинга (IL/XL: по времени; T: по пространству).
    std::vector<float> readInlineSliceProcessed(int il_idx, const CropBounds& crop,
                                                const ResampleParams& resample, int& out_w,
                                                int& out_h, std::vector<int32_t>& horiz_labels,
                                                std::vector<int32_t>& vert_labels) const;
    std::vector<float> readCrosslineSliceProcessed(int xl_idx, const CropBounds& crop,
                                                   const ResampleParams& resample, int& out_w,
                                                   int& out_h, std::vector<int32_t>& horiz_labels,
                                                   std::vector<int32_t>& vert_labels) const;
    std::vector<float> readTimeSliceProcessed(int t_idx, const CropBounds& crop,
                                              const ResampleParams& resample, int& out_w, int& out_h,
                                              std::vector<int32_t>& horiz_labels,
                                              std::vector<int32_t>& vert_labels) const;

    /// Одна трасса после кропа по времени и ресемплинга.
    std::vector<float> readTraceProcessed(int il_idx, int xl_idx, const CropBounds& crop,
                                          const ResampleParams& resample, float& out_dt_ms) const;

    /// Перцентиль амплитуды (0…100) по центральному inline.
    float amplitudePercentile(float percentile) const;
    /// clip=99 → [p0.5, p99.5]; clip=95 → [p2.5, p97.5]; clip=1 → [p49.5, p50.5].
    void clipRange(float clip_percent, float& out_vmin, float& out_vmax) const;

    /// Кроп, ресемплинг (время → пространство), опционально 1D/2D FFT; выход IEEE float (SEGY_BIN_FORMAT=5).
    void saveCropped(const std::string& out_path, const CropBounds& crop,
                     const ResampleParams& resample = {},
                     const FftFilterParams* fft_filter = nullptr,
                     const FftFilter2DParams* fft_filter2d = nullptr,
                     const SaveCroppedProgressCallback& progress = {}) const;

    /// Статистика координат съёмки (inline/crossline/CDP, азимут, углы).
    SurveyCoordinateStats surveyCoordinates() const;

private:
    int traceId(int il_idx, int xl_idx) const;
    std::size_t volumeOffset(int il_idx, int xl_idx, int t_idx) const;
    std::vector<float> readTraceAt(int il_idx, int xl_idx) const;
    void loadVolumeToMemory(const CubeLoadProgressCallback& progress);
    void buildAmplitudeStatsFromInline(int il_idx);

    std::string path_;
    CubeGeometry geom_{};
    FileHeaders headers_{};
    std::vector<int32_t> inlines_;
    std::vector<int32_t> crosslines_;
    std::vector<int32_t> trace_ids_;
    std::vector<double> cdp_x_;
    std::vector<double> cdp_y_;
    std::vector<float> volume_;
    std::vector<float> amplitudes_sorted_;
    CubeLoadMode load_mode_ = CubeLoadMode::Lazy;
    int sample_format_ = 0;
    int elemsize_ = 4;
    bool loaded_ = false;
};

}  // namespace kubik
