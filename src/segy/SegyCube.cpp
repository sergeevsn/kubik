#include "kubik/SegyCube.hpp"
#include "kubik/AmplitudeClip.hpp"
#include "kubik/Resample.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

#include <segyio/segy.h>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace kubik {

namespace {

constexpr std::uint64_t kSegyFileHeaderSize = 3600;

segy_file* openForReading(const std::string& path) {
    segy_file* fp = segy_open(path.c_str(), "rb");
    if (!fp) {
        return nullptr;
    }
    if (segy_collect_metadata(fp, -1, -1, -1) != SEGY_OK) {
        segy_close(fp);
        return nullptr;
    }
    return fp;
}

bool readTraceFieldIntRelaxed(const char* header, int field, int32_t* out) {
    if (!header || !out) {
        return false;
    }
    int v = 0;
    if (segy_get_tracefield_int(header, field, &v) == SEGY_OK) {
        *out = static_cast<int32_t>(v);
        return true;
    }
    segy_field_data fd{};
    if (segy_get_tracefield(header, segy_traceheader_default_map(), field, &fd) != SEGY_OK) {
        return false;
    }
    const int dt = segy_entry_type_to_datatype(fd.entry_type);
    switch (dt) {
    case SEGY_IEEE_FLOAT_4_BYTE:
    case SEGY_IBM_FLOAT_4_BYTE:
        if (!std::isfinite(fd.value.f32)) {
            return false;
        }
        *out = static_cast<int32_t>(std::lround(fd.value.f32));
        return true;
    case SEGY_IEEE_FLOAT_8_BYTE:
        if (!std::isfinite(fd.value.f64)) {
            return false;
        }
        *out = static_cast<int32_t>(std::llround(fd.value.f64));
        return true;
    default:
        return false;
    }
}

bool readTraceFieldDoubleRelaxed(const char* header, int field, double* out) {
    if (!header || !out) {
        return false;
    }
    int32_t iv = 0;
    if (readTraceFieldIntRelaxed(header, field, &iv)) {
        *out = static_cast<double>(iv);
        return true;
    }
    segy_field_data fd{};
    if (segy_get_tracefield(header, segy_traceheader_default_map(), field, &fd) != SEGY_OK) {
        return false;
    }
    const int dt = segy_entry_type_to_datatype(fd.entry_type);
    switch (dt) {
    case SEGY_IEEE_FLOAT_4_BYTE:
    case SEGY_IBM_FLOAT_4_BYTE:
        if (!std::isfinite(fd.value.f32)) {
            return false;
        }
        *out = static_cast<double>(fd.value.f32);
        return true;
    case SEGY_IEEE_FLOAT_8_BYTE:
        if (!std::isfinite(fd.value.f64)) {
            return false;
        }
        *out = fd.value.f64;
        return true;
    default:
        return false;
    }
}

std::vector<float> readTraceSamples(segy_file* fp,
                                    int trace_id,
                                    int n_t,
                                    int sample_format,
                                    int elemsize) {
    if (!fp || trace_id < 0 || n_t <= 0 || elemsize <= 0) {
        return {};
    }

    std::vector<float> samples(static_cast<std::size_t>(n_t), 0.f);
    std::vector<char> raw(static_cast<std::size_t>(n_t) * static_cast<std::size_t>(elemsize));
    if (segy_readtrace(fp, trace_id, raw.data()) != SEGY_OK) {
        return samples;
    }
    // Формат берём из binary header (IBM / IEEE / …), не угадываем.
    if (segy_to_native(sample_format, static_cast<long long>(n_t), raw.data()) != SEGY_OK) {
        return samples;
    }

    if (elemsize == 4) {
        const float* src = reinterpret_cast<const float*>(raw.data());
        for (int i = 0; i < n_t; ++i) {
            samples[static_cast<std::size_t>(i)] = src[i];
        }
    } else if (elemsize == 8) {
        const double* src = reinterpret_cast<const double*>(raw.data());
        for (int i = 0; i < n_t; ++i) {
            samples[static_cast<std::size_t>(i)] = static_cast<float>(src[i]);
        }
    }
    return samples;
}

int readSampleFormatFromBinary(const char* binary) {
    return segy_format(binary);
}

int estimateSaveWorkTotal(int n_traces_crop, int n_t_out, int n_traces_out, bool spatial_resample,
                          bool fft1d, bool fft2d) {
    int total = 1;
    total += n_traces_crop;
    if (spatial_resample) {
        total += n_t_out;
    }
    if (fft1d) {
        total += n_traces_out;
    }
    if (fft2d) {
        total += n_t_out;
    }
    total += n_traces_out;
    return std::max(1, total);
}

struct SaveProgressTracker {
    SaveCroppedProgressCallback cb;
    int overall_total = 1;
    int overall_current = 0;

    explicit SaveProgressTracker(const SaveCroppedProgressCallback& callback) : cb(callback) {}

    void setTotal(int total) { overall_total = std::max(1, total); }

    [[nodiscard]] bool report(SaveCroppedProgress::Stage stage, int stage_current, int stage_total) {
        if (!cb) {
            return true;
        }
        SaveCroppedProgress info;
        info.stage = stage;
        info.stage_current = stage_current;
        info.stage_total = std::max(1, stage_total);
        info.overall_current = overall_current;
        info.overall_total = overall_total;
        return cb(info);
    }

    bool advance(SaveCroppedProgress::Stage stage, int stage_current, int stage_total) {
        if (overall_current < overall_total) {
            ++overall_current;
        }
        return report(stage, stage_current, stage_total);
    }
};

}  // namespace

SegyCube::~SegyCube() {
    close();
}

void SegyCube::close() {
    path_.clear();
    geom_ = {};
    headers_ = {};
    inlines_.clear();
    crosslines_.clear();
    trace_ids_.clear();
    cdp_x_.clear();
    cdp_y_.clear();
    volume_.clear();
    amplitudes_sorted_.clear();
    load_mode_ = CubeLoadMode::Lazy;
    sample_format_ = 0;
    elemsize_ = 4;
    loaded_ = false;
}

void SegyCube::load(const std::string& path, const CubeLoadOptions& options) {
    close();
    load_mode_ = options.mode;
    const int inline_field = options.inline_field;
    const int crossline_field = options.crossline_field;

    segy_file* fp = segy_open(path.c_str(), "rb");
    if (!fp) {
        throw std::runtime_error("SegyCube: cannot open " + path);
    }
    if (segy_read_textheader(fp, headers_.textual) != SEGY_OK) {
        std::memset(headers_.textual, 0, kTextHeaderSize);
    }
    if (segy_binheader(fp, headers_.binary) != SEGY_OK) {
        segy_close(fp);
        throw std::runtime_error("SegyCube: cannot read binary header");
    }

    sample_format_ = readSampleFormatFromBinary(headers_.binary);
    elemsize_ = segy_formatsize(sample_format_);
    if (elemsize_ <= 0) {
        segy_close(fp);
        throw std::runtime_error(
            "SegyCube: unsupported sample format in binary header (SEGY_BIN_FORMAT=" +
            std::to_string(sample_format_) + ")");
    }

    const int n_t = segy_samples(headers_.binary);
    if (n_t <= 0) {
        segy_close(fp);
        throw std::runtime_error("SegyCube: invalid sample count in binary header");
    }

    int bin_interval_us = 0;
    (void)segy_get_binfield_int(headers_.binary, SEGY_BIN_INTERVAL, &bin_interval_us);
    geom_.dt_ms = bin_interval_us > 0 ? static_cast<float>(bin_interval_us) / 1000.f : 1.f;
    geom_.n_t = n_t;
    geom_.sample_format = sample_format_;

    if (segy_collect_metadata(fp, -1, -1, -1) != SEGY_OK) {
        segy_close(fp);
        throw std::runtime_error("SegyCube: segy_collect_metadata failed");
    }

    const int n_traces = fp->metadata.tracecount;
    if (n_traces <= 0) {
        segy_close(fp);
        throw std::runtime_error("SegyCube: empty file");
    }
    if (fp->metadata.samplecount != n_t) {
        segy_close(fp);
        throw std::runtime_error("SegyCube: binary header samples (" + std::to_string(n_t) +
                                 ") != metadata samplecount (" +
                                 std::to_string(fp->metadata.samplecount) + ")");
    }

    const std::size_t trace_data_size =
        static_cast<std::size_t>(n_t) * static_cast<std::size_t>(elemsize_);
    const std::size_t full_trace_size = kTraceHeaderSize + trace_data_size;

    segy_close(fp);

    int32_t min_il = INT_MAX, max_il = INT_MIN;
    int32_t min_xl = INT_MAX, max_xl = INT_MIN;

    struct TraceMeta {
        int32_t il = 0;
        int32_t xl = 0;
        double cdp_x = 0.0;
        double cdp_y = 0.0;
        bool ok = false;
        bool has_cdp = false;
    };
    std::vector<TraceMeta> meta(static_cast<std::size_t>(n_traces));

    const CubeLoadProgressCallback& progress_cb = options.progress;
    const int scan_progress_stride = std::max(1, n_traces / 200);
    auto reportScanProgress = [&](int current) -> bool {
        if (!progress_cb) {
            return true;
        }
        CubeLoadProgress info;
        info.stage = CubeLoadProgress::Stage::ScanHeaders;
        info.current = current;
        info.total = n_traces;
        return progress_cb(info);
    };
    if (!reportScanProgress(0)) {
        throw LoadCanceled();
    }

    std::atomic<bool> scan_canceled{false};
    std::atomic<int> traces_scanned{0};

#ifdef _OPENMP
#pragma omp parallel
    {
        std::ifstream is(path, std::ios::binary);
        if (!is) {
#pragma omp critical
            { throw std::runtime_error("SegyCube: cannot open file for header scan"); }
        }

        std::vector<char> header_buf(kTraceHeaderSize);
        int32_t local_min_il = INT_MAX, local_max_il = INT_MIN;
        int32_t local_min_xl = INT_MAX, local_max_xl = INT_MIN;

#pragma omp for schedule(static)
        for (int tr = 0; tr < n_traces; ++tr) {
            if (scan_canceled.load(std::memory_order_relaxed)) {
                continue;
            }

            const std::uint64_t offset =
                kSegyFileHeaderSize + static_cast<std::uint64_t>(tr) * full_trace_size;
            is.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            is.read(header_buf.data(), static_cast<std::streamsize>(kTraceHeaderSize));
            if (is) {
                int32_t il = 0, xl = 0;
                const bool ok_il = readTraceFieldIntRelaxed(header_buf.data(), inline_field, &il);
                const bool ok_xl = readTraceFieldIntRelaxed(header_buf.data(), crossline_field, &xl);
                if (ok_il && ok_xl) {
                    TraceMeta tm;
                    tm.il = il;
                    tm.xl = xl;
                    tm.ok = true;
                    double cx = 0.0;
                    double cy = 0.0;
                    if (readTraceFieldDoubleRelaxed(header_buf.data(), SEGY_TR_CDP_X, &cx) &&
                        readTraceFieldDoubleRelaxed(header_buf.data(), SEGY_TR_CDP_Y, &cy)) {
                        tm.cdp_x = cx;
                        tm.cdp_y = cy;
                        tm.has_cdp = true;
                    }
                    meta[static_cast<std::size_t>(tr)] = tm;

                    if (il < local_min_il) local_min_il = il;
                    if (il > local_max_il) local_max_il = il;
                    if (xl < local_min_xl) local_min_xl = xl;
                    if (xl > local_max_xl) local_max_xl = xl;
                }
            }

            const int done = traces_scanned.fetch_add(1, std::memory_order_relaxed) + 1;
            if (progress_cb && (done % scan_progress_stride == 0 || done == n_traces)) {
#pragma omp critical(scan_progress)
                {
                    if (!scan_canceled.load(std::memory_order_relaxed) && !reportScanProgress(done)) {
                        scan_canceled.store(true, std::memory_order_relaxed);
                    }
                }
            }
        }

#pragma omp critical
        {
            if (local_min_il <= local_max_il) {
                min_il = std::min(min_il, local_min_il);
                max_il = std::max(max_il, local_max_il);
            }
            if (local_min_xl <= local_max_xl) {
                min_xl = std::min(min_xl, local_min_xl);
                max_xl = std::max(max_xl, local_max_xl);
            }
        }
    }
#else
    {
        std::ifstream is(path, std::ios::binary);
        if (!is) {
            throw std::runtime_error("SegyCube: cannot open file for header scan");
        }
        std::vector<char> header_buf(kTraceHeaderSize);
        for (int tr = 0; tr < n_traces; ++tr) {
            const std::uint64_t offset =
                kSegyFileHeaderSize + static_cast<std::uint64_t>(tr) * full_trace_size;
            is.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            is.read(header_buf.data(), static_cast<std::streamsize>(kTraceHeaderSize));
            if (is) {
                int32_t il = 0, xl = 0;
                if (readTraceFieldIntRelaxed(header_buf.data(), inline_field, &il) &&
                    readTraceFieldIntRelaxed(header_buf.data(), crossline_field, &xl)) {
                    TraceMeta& tm = meta[static_cast<std::size_t>(tr)];
                    tm.il = il;
                    tm.xl = xl;
                    tm.ok = true;
                    double cx = 0.0;
                    double cy = 0.0;
                    if (readTraceFieldDoubleRelaxed(header_buf.data(), SEGY_TR_CDP_X, &cx) &&
                        readTraceFieldDoubleRelaxed(header_buf.data(), SEGY_TR_CDP_Y, &cy)) {
                        tm.cdp_x = cx;
                        tm.cdp_y = cy;
                        tm.has_cdp = true;
                    }
                    min_il = std::min(min_il, il);
                    max_il = std::max(max_il, il);
                    min_xl = std::min(min_xl, xl);
                    max_xl = std::max(max_xl, xl);
                }
            }

            const int done = tr + 1;
            if (progress_cb && (done % scan_progress_stride == 0 || done == n_traces)) {
                if (!reportScanProgress(done)) {
                    throw LoadCanceled();
                }
            }
        }
    }
#endif

    if (scan_canceled.load(std::memory_order_relaxed)) {
        throw LoadCanceled();
    }
    if (!reportScanProgress(n_traces)) {
        throw LoadCanceled();
    }

    if (min_il > max_il || min_xl > max_xl) {
        throw std::runtime_error("SegyCube: cannot determine inline/crossline extent");
    }

    std::set<int32_t> il_set;
    std::set<int32_t> xl_set;
    for (const TraceMeta& tm : meta) {
        if (!tm.ok) {
            continue;
        }
        il_set.insert(tm.il);
        xl_set.insert(tm.xl);
    }
    if (il_set.empty() || xl_set.empty()) {
        throw std::runtime_error("SegyCube: no traces with valid inline/crossline");
    }

    inlines_.assign(il_set.begin(), il_set.end());
    crosslines_.assign(xl_set.begin(), xl_set.end());

    geom_.min_il = inlines_.front();
    geom_.max_il = inlines_.back();
    geom_.min_xl = crosslines_.front();
    geom_.max_xl = crosslines_.back();
    geom_.n_il = static_cast<int>(inlines_.size());
    geom_.n_xl = static_cast<int>(crosslines_.size());

    const std::size_t grid_size =
        static_cast<std::size_t>(geom_.n_il) * static_cast<std::size_t>(geom_.n_xl);
    trace_ids_.assign(grid_size, -1);
    cdp_x_.assign(grid_size, std::numeric_limits<double>::quiet_NaN());
    cdp_y_.assign(grid_size, std::numeric_limits<double>::quiet_NaN());
    for (int tr = 0; tr < n_traces; ++tr) {
        const TraceMeta& tm = meta[static_cast<std::size_t>(tr)];
        if (!tm.ok) {
            continue;
        }
        const auto il_it = std::lower_bound(inlines_.begin(), inlines_.end(), tm.il);
        const auto xl_it = std::lower_bound(crosslines_.begin(), crosslines_.end(), tm.xl);
        if (il_it == inlines_.end() || *il_it != tm.il || xl_it == crosslines_.end() || *xl_it != tm.xl) {
            continue;
        }
        const int il_idx = static_cast<int>(il_it - inlines_.begin());
        const int xl_idx = static_cast<int>(xl_it - crosslines_.begin());
        const std::size_t flat =
            static_cast<std::size_t>(il_idx) * static_cast<std::size_t>(geom_.n_xl) +
            static_cast<std::size_t>(xl_idx);
        trace_ids_[flat] = tr;
        if (tm.has_cdp) {
            cdp_x_[flat] = tm.cdp_x;
            cdp_y_[flat] = tm.cdp_y;
        }
    }

    path_ = path;
    loaded_ = true;

    switch (options.mode) {
    case CubeLoadMode::Lazy: {
        if (options.progress) {
            CubeLoadProgress info;
            info.stage = CubeLoadProgress::Stage::BuildStats;
            info.current = 0;
            info.total = 1;
            if (!options.progress(info)) {
                close();
                throw LoadCanceled();
            }
        }
        const int il_idx = std::max(0, geom_.n_il / 2);
        buildAmplitudeStatsFromInline(il_idx);
        break;
    }
    case CubeLoadMode::InMemory:
        loadVolumeToMemory(options.progress);
        buildAmplitudeStatsFromVolume();
        break;
    }
}

std::size_t SegyCube::volumeOffset(int il_idx, int xl_idx, int t_idx) const {
    return (static_cast<std::size_t>(il_idx) * static_cast<std::size_t>(geom_.n_xl) +
            static_cast<std::size_t>(xl_idx)) *
               static_cast<std::size_t>(geom_.n_t) +
           static_cast<std::size_t>(t_idx);
}

std::vector<float> SegyCube::readTraceAt(int il_idx, int xl_idx) const {
    const int nt = geom_.n_t;
    if (!loaded_ || il_idx < 0 || xl_idx < 0 || il_idx >= geom_.n_il || xl_idx >= geom_.n_xl ||
        nt <= 0) {
        return {};
    }

    if (!volume_.empty()) {
        const std::size_t base = volumeOffset(il_idx, xl_idx, 0);
        return std::vector<float>(volume_.begin() + base, volume_.begin() + base + nt);
    }

    const int tid = traceId(il_idx, xl_idx);
    if (tid < 0) {
        return std::vector<float>(static_cast<std::size_t>(nt), 0.f);
    }

    segy_file* fp = openForReading(path_);
    if (!fp) {
        return std::vector<float>(static_cast<std::size_t>(nt), 0.f);
    }
    std::vector<float> samples = readTraceSamples(fp, tid, nt, sample_format_, elemsize_);
    segy_close(fp);
    return samples;
}

void SegyCube::loadVolumeToMemory(const CubeLoadProgressCallback& progress) {
    const int n_il = geom_.n_il;
    const int n_xl = geom_.n_xl;
    const int nt = geom_.n_t;
    if (n_il <= 0 || n_xl <= 0 || nt <= 0) {
        return;
    }

    const std::size_t vol_size =
        static_cast<std::size_t>(n_il) * static_cast<std::size_t>(n_xl) * static_cast<std::size_t>(nt);
    volume_.assign(vol_size, 0.f);

    const int total = n_il * n_xl;
    if (progress) {
        CubeLoadProgress info;
        info.stage = CubeLoadProgress::Stage::LoadVolume;
        info.current = 0;
        info.total = total;
        if (!progress(info)) {
            volume_.clear();
            throw LoadCanceled();
        }
    }

#ifdef _OPENMP
#pragma omp parallel
    {
        segy_file* fp = openForReading(path_);
#pragma omp for collapse(2) schedule(dynamic)
        for (int il = 0; il < n_il; ++il) {
            for (int xl = 0; xl < n_xl; ++xl) {
                const int tr = traceId(il, xl);
                if (tr < 0) {
                    continue;
                }
                const std::vector<float> samples =
                    readTraceSamples(fp, tr, nt, sample_format_, elemsize_);
                const std::size_t base = volumeOffset(il, xl, 0);
                std::copy(samples.begin(), samples.end(), volume_.begin() + base);
            }
        }
        if (fp) {
            segy_close(fp);
        }
    }
#else
    {
        segy_file* fp = openForReading(path_);
        int done = 0;
        for (int il = 0; il < n_il; ++il) {
            for (int xl = 0; xl < n_xl; ++xl) {
                const int tr = traceId(il, xl);
                if (tr >= 0) {
                    const std::vector<float> samples =
                        readTraceSamples(fp, tr, nt, sample_format_, elemsize_);
                    const std::size_t base = volumeOffset(il, xl, 0);
                    std::copy(samples.begin(), samples.end(), volume_.begin() + base);
                }
                ++done;
                if (progress && done % 64 == 0) {
                    CubeLoadProgress info;
                    info.stage = CubeLoadProgress::Stage::LoadVolume;
                    info.current = done;
                    info.total = total;
                    if (!progress(info)) {
                        if (fp) {
                            segy_close(fp);
                        }
                        volume_.clear();
                        throw LoadCanceled();
                    }
                }
            }
        }
        if (fp) {
            segy_close(fp);
        }
    }
#endif

    if (progress) {
        CubeLoadProgress info;
        info.stage = CubeLoadProgress::Stage::LoadVolume;
        info.current = total;
        info.total = total;
        if (!progress(info)) {
            volume_.clear();
            throw LoadCanceled();
        }
        info.stage = CubeLoadProgress::Stage::BuildStats;
        info.current = 0;
        info.total = 1;
        if (!progress(info)) {
            volume_.clear();
            throw LoadCanceled();
        }
    }
}

void SegyCube::buildAmplitudeStatsFromInline(int il_idx) {
    amplitudes_sorted_.clear();
    if (!loaded_ || geom_.n_t <= 0) {
        return;
    }

    il_idx = std::clamp(il_idx, 0, std::max(0, geom_.n_il - 1));
    const std::vector<float> slice = readInlineSlice(il_idx);
    amplitudes_sorted_.reserve(slice.size());
    for (float v : slice) {
        if (std::isfinite(v)) {
            amplitudes_sorted_.push_back(v);
        }
    }
    std::sort(amplitudes_sorted_.begin(), amplitudes_sorted_.end());
}

void SegyCube::buildAmplitudeStatsFromVolume() {
    amplitudes_sorted_.clear();
    if (volume_.empty()) {
        return;
    }

    amplitudes_sorted_.reserve(volume_.size());
    for (float v : volume_) {
        if (std::isfinite(v)) {
            amplitudes_sorted_.push_back(v);
        }
    }
    std::sort(amplitudes_sorted_.begin(), amplitudes_sorted_.end());
}

float SegyCube::amplitudePercentile(float percentile) const {
    if (amplitudes_sorted_.empty()) {
        return 0.f;
    }
    percentile = std::clamp(percentile, 0.f, 100.f);
    const std::size_t n = amplitudes_sorted_.size();
    if (n == 1) {
        return amplitudes_sorted_.front();
    }
    const double idx = static_cast<double>(percentile) / 100.0 * static_cast<double>(n - 1);
    const std::size_t i0 = static_cast<std::size_t>(std::floor(idx));
    const std::size_t i1 = std::min(i0 + 1, n - 1);
    const double t = idx - static_cast<double>(i0);
    return static_cast<float>((1.0 - t) * static_cast<double>(amplitudes_sorted_[i0]) +
                              t * static_cast<double>(amplitudes_sorted_[i1]));
}

void SegyCube::clipRange(float clip_percent, float& out_vmin, float& out_vmax) const {
    amplitudeClipRangeFromSorted(amplitudes_sorted_, clip_percent, out_vmin, out_vmax);
}

int SegyCube::traceId(int il_idx, int xl_idx) const {
    if (il_idx < 0 || xl_idx < 0 || il_idx >= geom_.n_il || xl_idx >= geom_.n_xl) {
        return -1;
    }
    const std::size_t flat =
        static_cast<std::size_t>(il_idx) * static_cast<std::size_t>(geom_.n_xl) +
        static_cast<std::size_t>(xl_idx);
    return trace_ids_[flat];
}

int32_t SegyCube::inlineLabel(int il_idx) const {
    if (il_idx < 0 || il_idx >= static_cast<int>(inlines_.size())) {
        return 0;
    }
    return inlines_[static_cast<std::size_t>(il_idx)];
}

int32_t SegyCube::crosslineLabel(int xl_idx) const {
    if (xl_idx < 0 || xl_idx >= static_cast<int>(crosslines_.size())) {
        return 0;
    }
    return crosslines_[static_cast<std::size_t>(xl_idx)];
}

int SegyCube::inlineIndex(int32_t il) const {
    const auto it = std::lower_bound(inlines_.begin(), inlines_.end(), il);
    if (it == inlines_.end() || *it != il) {
        return -1;
    }
    return static_cast<int>(it - inlines_.begin());
}

int SegyCube::crosslineIndex(int32_t xl) const {
    const auto it = std::lower_bound(crosslines_.begin(), crosslines_.end(), xl);
    if (it == crosslines_.end() || *it != xl) {
        return -1;
    }
    return static_cast<int>(it - crosslines_.begin());
}

int SegyCube::timeMs(int t_idx) const {
    if (t_idx < 0 || t_idx >= geom_.n_t || geom_.dt_ms <= 0.f) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<double>(t_idx) * static_cast<double>(geom_.dt_ms)));
}

std::vector<float> SegyCube::readInlineSlice(int il_idx) const {
    if (!loaded_ || il_idx < 0 || il_idx >= geom_.n_il) {
        return {};
    }
    const int w = geom_.n_xl;
    const int h = geom_.n_t;
    std::vector<float> slice(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.f);

    if (!volume_.empty()) {
        for (int xl = 0; xl < w; ++xl) {
            const std::size_t base = volumeOffset(il_idx, xl, 0);
            for (int t = 0; t < h; ++t) {
                slice[static_cast<std::size_t>(t) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(xl)] = volume_[base + static_cast<std::size_t>(t)];
            }
        }
        return slice;
    }

#ifdef _OPENMP
#pragma omp parallel
    {
        segy_file* fp = openForReading(path_);
#pragma omp for schedule(dynamic)
        for (int xl = 0; xl < w; ++xl) {
            const int tid = traceId(il_idx, xl);
            if (tid < 0) {
                continue;
            }
            const std::vector<float> tr =
                readTraceSamples(fp, tid, h, sample_format_, elemsize_);
            for (int t = 0; t < h; ++t) {
                slice[static_cast<std::size_t>(t) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(xl)] = tr[static_cast<std::size_t>(t)];
            }
        }
        if (fp) {
            segy_close(fp);
        }
    }
#else
    segy_file* fp = openForReading(path_);
    for (int xl = 0; xl < w; ++xl) {
        const int tid = traceId(il_idx, xl);
        if (tid < 0) {
            continue;
        }
        const std::vector<float> tr =
            readTraceSamples(fp, tid, h, sample_format_, elemsize_);
        for (int t = 0; t < h; ++t) {
            slice[static_cast<std::size_t>(t) * static_cast<std::size_t>(w) +
                  static_cast<std::size_t>(xl)] = tr[static_cast<std::size_t>(t)];
        }
    }
    if (fp) {
        segy_close(fp);
    }
#endif
    return slice;
}

std::vector<float> SegyCube::readCrosslineSlice(int xl_idx) const {
    if (!loaded_ || xl_idx < 0 || xl_idx >= geom_.n_xl) {
        return {};
    }
    const int w = geom_.n_il;
    const int h = geom_.n_t;
    std::vector<float> slice(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.f);

    if (!volume_.empty()) {
        for (int il = 0; il < w; ++il) {
            const std::size_t base = volumeOffset(il, xl_idx, 0);
            for (int t = 0; t < h; ++t) {
                slice[static_cast<std::size_t>(t) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(il)] = volume_[base + static_cast<std::size_t>(t)];
            }
        }
        return slice;
    }

#ifdef _OPENMP
#pragma omp parallel
    {
        segy_file* fp = openForReading(path_);
#pragma omp for schedule(dynamic)
        for (int il = 0; il < w; ++il) {
            const int tid = traceId(il, xl_idx);
            if (tid < 0) {
                continue;
            }
            const std::vector<float> tr =
                readTraceSamples(fp, tid, h, sample_format_, elemsize_);
            for (int t = 0; t < h; ++t) {
                slice[static_cast<std::size_t>(t) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(il)] = tr[static_cast<std::size_t>(t)];
            }
        }
        if (fp) {
            segy_close(fp);
        }
    }
#else
    segy_file* fp = openForReading(path_);
    for (int il = 0; il < w; ++il) {
        const int tid = traceId(il, xl_idx);
        if (tid < 0) {
            continue;
        }
        const std::vector<float> tr =
            readTraceSamples(fp, tid, h, sample_format_, elemsize_);
        for (int t = 0; t < h; ++t) {
            slice[static_cast<std::size_t>(t) * static_cast<std::size_t>(w) +
                  static_cast<std::size_t>(il)] = tr[static_cast<std::size_t>(t)];
        }
    }
    if (fp) {
        segy_close(fp);
    }
#endif
    return slice;
}

std::vector<float> SegyCube::readTimeSlice(int t_idx) const {
    if (!loaded_ || t_idx < 0 || t_idx >= geom_.n_t) {
        return {};
    }
    const int w = geom_.n_xl;
    const int h = geom_.n_il;
    std::vector<float> slice(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.f);

    if (!volume_.empty()) {
        for (int il = 0; il < h; ++il) {
            for (int xl = 0; xl < w; ++xl) {
                slice[static_cast<std::size_t>(il) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(xl)] = volume_[volumeOffset(il, xl, t_idx)];
            }
        }
        return slice;
    }

#ifdef _OPENMP
#pragma omp parallel
    {
        segy_file* fp = openForReading(path_);
#pragma omp for schedule(dynamic)
        for (int il = 0; il < h; ++il) {
            for (int xl = 0; xl < w; ++xl) {
                const int tid = traceId(il, xl);
                if (tid < 0) {
                    continue;
                }
                const std::vector<float> tr =
                    readTraceSamples(fp, tid, geom_.n_t, sample_format_, elemsize_);
                slice[static_cast<std::size_t>(il) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(xl)] = tr[static_cast<std::size_t>(t_idx)];
            }
        }
        if (fp) {
            segy_close(fp);
        }
    }
#else
    segy_file* fp = openForReading(path_);
    for (int il = 0; il < h; ++il) {
        for (int xl = 0; xl < w; ++xl) {
            const int tid = traceId(il, xl);
            if (tid < 0) {
                continue;
            }
            const std::vector<float> tr =
                readTraceSamples(fp, tid, geom_.n_t, sample_format_, elemsize_);
            slice[static_cast<std::size_t>(il) * static_cast<std::size_t>(w) +
                  static_cast<std::size_t>(xl)] = tr[static_cast<std::size_t>(t_idx)];
        }
    }
    if (fp) {
        segy_close(fp);
    }
#endif
    return slice;
}

NativeGridSteps SegyCube::nativeGridSteps() const {
    NativeGridSteps steps;
    steps.dt_ms = geom_.dt_ms;
    steps.d_inline = uniformLabelStep(inlines_);
    steps.d_crossline = uniformLabelStep(crosslines_);
    return steps;
}

namespace {

std::vector<int32_t> sliceLabels(const std::vector<int32_t>& labels, int i0, int i1) {
    if (labels.empty() || i0 > i1) {
        return {};
    }
    i0 = std::clamp(i0, 0, static_cast<int>(labels.size()) - 1);
    i1 = std::clamp(i1, 0, static_cast<int>(labels.size()) - 1);
    if (i0 > i1) {
        std::swap(i0, i1);
    }
    return std::vector<int32_t>(labels.begin() + i0, labels.begin() + i1 + 1);
}

std::vector<float> cropSlice2D(const std::vector<float>& data, int w, int h, int x0, int x1, int y0,
                               int y1, int& out_w, int& out_h) {
    out_w = 0;
    out_h = 0;
    if (data.empty() || w <= 0 || h <= 0) {
        return {};
    }
    x0 = std::clamp(x0, 0, w - 1);
    x1 = std::clamp(x1, 0, w - 1);
    y0 = std::clamp(y0, 0, h - 1);
    y1 = std::clamp(y1, 0, h - 1);
    if (x0 > x1) {
        std::swap(x0, x1);
    }
    if (y0 > y1) {
        std::swap(y0, y1);
    }
    out_w = x1 - x0 + 1;
    out_h = y1 - y0 + 1;
    std::vector<float> out(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h));
    for (int y = 0; y < out_h; ++y) {
        for (int x = 0; x < out_w; ++x) {
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(out_w) + static_cast<std::size_t>(x)] =
                data[static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(w) +
                     static_cast<std::size_t>(x0 + x)];
        }
    }
    return out;
}

std::vector<float> resampleColumnsTime(const std::vector<float>& data, int w, int h, int label_t_min,
                                       int label_t_max, float dt_in_ms, float dt_out_ms,
                                       std::vector<int32_t>& vert_labels, int& out_h) {
    out_h = 0;
    if (w <= 0 || h <= 0) {
        return {};
    }
    int n_t_out = 0;
    std::vector<float> col_in(static_cast<std::size_t>(h));
    std::vector<float> out;
    for (int x = 0; x < w; ++x) {
        for (int t = 0; t < h; ++t) {
            col_in[static_cast<std::size_t>(t)] =
                data[static_cast<std::size_t>(t) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
        }
        int col_out_n = 0;
        std::vector<float> col_out =
            resampleTrace1D(col_in.data(), h, dt_in_ms, 0, h - 1, dt_out_ms, col_out_n);
        if (x == 0) {
            n_t_out = col_out_n;
            out.assign(static_cast<std::size_t>(n_t_out) * static_cast<std::size_t>(w), 0.f);
            vert_labels = buildTimeMsAxis(label_t_min, label_t_max, dt_in_ms, dt_out_ms);
            out_h = n_t_out;
        }
        for (int t = 0; t < col_out_n && t < n_t_out; ++t) {
            out[static_cast<std::size_t>(t) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                col_out[static_cast<std::size_t>(t)];
        }
    }
    return out;
}

}  // namespace

std::vector<float> SegyCube::readInlineSliceProcessed(int il_idx, const CropBounds& crop,
                                                      const ResampleParams& resample, int& out_w,
                                                      int& out_h, std::vector<int32_t>& horiz_labels,
                                                      std::vector<int32_t>& vert_labels) const {
    out_w = 0;
    out_h = 0;
    horiz_labels.clear();
    vert_labels.clear();
    if (!loaded_ || il_idx < 0 || il_idx >= geom_.n_il) {
        return {};
    }

    const std::vector<float> full = readInlineSlice(il_idx);
    const int w = geom_.n_xl;
    const int h = geom_.n_t;
    int cw = 0;
    int ch = 0;
    const std::vector<float> cropped =
        cropSlice2D(full, w, h, crop.xl_min, crop.xl_max, crop.t_min, crop.t_max, cw, ch);
    horiz_labels = sliceLabels(crosslines_, crop.xl_min, crop.xl_max);

    const float dt_out =
        resample.dt_out_ms > 0.f ? resample.dt_out_ms : geom_.dt_ms;
    out_w = cw;
    return resampleColumnsTime(cropped, cw, ch, crop.t_min, crop.t_max, geom_.dt_ms, dt_out,
                               vert_labels, out_h);
}

std::vector<float> SegyCube::readCrosslineSliceProcessed(int xl_idx, const CropBounds& crop,
                                                         const ResampleParams& resample, int& out_w,
                                                         int& out_h, std::vector<int32_t>& horiz_labels,
                                                         std::vector<int32_t>& vert_labels) const {
    out_w = 0;
    out_h = 0;
    horiz_labels.clear();
    vert_labels.clear();
    if (!loaded_ || xl_idx < 0 || xl_idx >= geom_.n_xl) {
        return {};
    }

    const std::vector<float> full = readCrosslineSlice(xl_idx);
    const int w = geom_.n_il;
    const int h = geom_.n_t;
    int cw = 0;
    int ch = 0;
    const std::vector<float> cropped =
        cropSlice2D(full, w, h, crop.il_min, crop.il_max, crop.t_min, crop.t_max, cw, ch);
    horiz_labels = sliceLabels(inlines_, crop.il_min, crop.il_max);

    const float dt_out =
        resample.dt_out_ms > 0.f ? resample.dt_out_ms : geom_.dt_ms;
    out_w = cw;
    return resampleColumnsTime(cropped, cw, ch, crop.t_min, crop.t_max, geom_.dt_ms, dt_out,
                               vert_labels, out_h);
}

std::vector<float> SegyCube::readTimeSliceProcessed(int t_idx, const CropBounds& crop,
                                                      const ResampleParams& resample, int& out_w,
                                                      int& out_h, std::vector<int32_t>& horiz_labels,
                                                      std::vector<int32_t>& vert_labels) const {
    out_w = 0;
    out_h = 0;
    horiz_labels.clear();
    vert_labels.clear();
    if (!loaded_ || t_idx < 0 || t_idx >= geom_.n_t) {
        return {};
    }

    const std::vector<float> full = readTimeSlice(t_idx);
    const int w = geom_.n_xl;
    const int h = geom_.n_il;
    int cw = 0;
    int ch = 0;
    const std::vector<float> cropped =
        cropSlice2D(full, w, h, crop.xl_min, crop.xl_max, crop.il_min, crop.il_max, cw, ch);
    const std::vector<int32_t> xl_labels = sliceLabels(crosslines_, crop.xl_min, crop.xl_max);
    const std::vector<int32_t> il_labels = sliceLabels(inlines_, crop.il_min, crop.il_max);

    const NativeGridSteps native = nativeGridSteps();
    const float d_il_out =
        resample.d_inline_out > 0.f ? resample.d_inline_out : native.d_inline;
    const float d_xl_out =
        resample.d_crossline_out > 0.f ? resample.d_crossline_out : native.d_crossline;

    if (xl_labels.empty() || il_labels.empty()) {
        return {};
    }

    return resampleSpatial2D(cropped.data(), cw, ch, xl_labels, il_labels, xl_labels.front(),
                             xl_labels.back(), il_labels.front(), il_labels.back(), d_xl_out,
                             d_il_out, horiz_labels, vert_labels, out_w, out_h);
}

std::vector<float> SegyCube::readTraceProcessed(int il_idx, int xl_idx, const CropBounds& crop,
                                                const ResampleParams& resample,
                                                float& out_dt_ms) const {
    out_dt_ms = resample.dt_out_ms > 0.f ? resample.dt_out_ms : geom_.dt_ms;
    if (!loaded_ || il_idx < 0 || il_idx >= geom_.n_il || xl_idx < 0 || xl_idx >= geom_.n_xl) {
        return {};
    }

    const int tid = traceId(il_idx, xl_idx);
    if (tid < 0 && volume_.empty()) {
        return {};
    }

    const std::vector<float> trace = readTraceAt(il_idx, xl_idx);
    if (trace.empty()) {
        return {};
    }

    const int t_lo = std::clamp(crop.t_min, 0, geom_.n_t - 1);
    const int t_hi = std::clamp(crop.t_max, 0, geom_.n_t - 1);
    if (t_lo > t_hi) {
        return {};
    }

    int n_out = 0;
    return resampleTrace1D(trace.data(), geom_.n_t, geom_.dt_ms, t_lo, t_hi, out_dt_ms, n_out);
}

void SegyCube::saveCropped(const std::string& out_path, const CropBounds& crop,
                           const ResampleParams& resample, const FftFilterParams* fft_filter,
                           const FftFilter2DParams* fft_filter2d,
                           const SaveCroppedProgressCallback& progress) const {
    if (!loaded_) {
        throw std::runtime_error("SegyCube::saveCropped: no cube loaded");
    }

    const int il_lo = std::clamp(crop.il_min, 0, geom_.n_il - 1);
    const int il_hi = std::clamp(crop.il_max, 0, geom_.n_il - 1);
    const int xl_lo = std::clamp(crop.xl_min, 0, geom_.n_xl - 1);
    const int xl_hi = std::clamp(crop.xl_max, 0, geom_.n_xl - 1);
    const int t_lo = std::clamp(crop.t_min, 0, geom_.n_t - 1);
    const int t_hi = std::clamp(crop.t_max, 0, geom_.n_t - 1);
    if (il_lo > il_hi || xl_lo > xl_hi || t_lo > t_hi) {
        throw std::runtime_error("SegyCube::saveCropped: invalid crop bounds");
    }

    const int n_il_crop = il_hi - il_lo + 1;
    const int n_xl_crop = xl_hi - xl_lo + 1;
    if (n_il_crop <= 0 || n_xl_crop <= 0) {
        throw std::runtime_error("SegyCube::saveCropped: empty crop region");
    }

    const NativeGridSteps native = nativeGridSteps();
    const float dt_out = resample.dt_out_ms > 0.f ? resample.dt_out_ms : geom_.dt_ms;
    const float d_il_out =
        resample.d_inline_out > 0.f ? resample.d_inline_out : native.d_inline;
    const float d_xl_out =
        resample.d_crossline_out > 0.f ? resample.d_crossline_out : native.d_crossline;

    const std::vector<int32_t> il_crop_labels = sliceLabels(inlines_, il_lo, il_hi);
    const std::vector<int32_t> xl_crop_labels = sliceLabels(crosslines_, xl_lo, xl_hi);
    if (il_crop_labels.empty() || xl_crop_labels.empty()) {
        throw std::runtime_error("SegyCube::saveCropped: empty inline/crossline labels");
    }

    segy_file* src = openForReading(path_);
    if (!src) {
        throw std::runtime_error("SegyCube::saveCropped: cannot open source " + path_);
    }
    const auto close_source = [&]() {
        if (src) {
            segy_close(src);
            src = nullptr;
        }
    };

    segy_file* dst = segy_open(out_path.c_str(), "w+b");
    if (!dst) {
        close_source();
        throw std::runtime_error("SegyCube::saveCropped: cannot create " + out_path);
    }

    if (src) {
        dst->metadata.encoding = src->metadata.encoding;
    }
    dst->metadata.endianness = SEGY_MSB;

    if (segy_write_textheader(dst, 0, headers_.textual) != SEGY_OK) {
        segy_close(dst);
        close_source();
        throw std::runtime_error("SegyCube::saveCropped: failed to write textual header");
    }

    int ext_text_headers = 0;
    (void)segy_get_binfield_int(headers_.binary, SEGY_BIN_EXT_HEADERS, &ext_text_headers);

    int n_t_out = 0;
    {
        std::vector<float> probe(static_cast<std::size_t>(geom_.n_t), 0.f);
        (void)resampleTrace1D(probe.data(), geom_.n_t, geom_.dt_ms, t_lo, t_hi, dt_out, n_t_out);
    }
    if (n_t_out <= 0) {
        segy_close(dst);
        close_source();
        throw std::runtime_error("SegyCube::saveCropped: empty time range after resample");
    }

    const bool spatial_resample = needsSpatialResample(native.d_inline, d_il_out) ||
                                  needsSpatialResample(native.d_crossline, d_xl_out);
    std::vector<int32_t> il_out_labels;
    std::vector<int32_t> xl_out_labels;
    int n_il_out = n_il_crop;
    int n_xl_out = n_xl_crop;
    if (spatial_resample) {
        il_out_labels = buildLabelAxis(il_crop_labels.front(), il_crop_labels.back(), d_il_out);
        xl_out_labels = buildLabelAxis(xl_crop_labels.front(), xl_crop_labels.back(), d_xl_out);
        n_il_out = static_cast<int>(il_out_labels.size());
        n_xl_out = static_cast<int>(xl_out_labels.size());
    } else {
        il_out_labels = il_crop_labels;
        xl_out_labels = xl_crop_labels;
    }

    const int n_traces_crop = n_il_crop * n_xl_crop;
    const int n_traces_out = n_il_out * n_xl_out;
    SaveProgressTracker tracker(progress);
    tracker.setTotal(estimateSaveWorkTotal(n_traces_crop, n_t_out, n_traces_out, spatial_resample,
                                           fft_filter != nullptr, fft_filter2d != nullptr));
    auto cancelSave = [&]() {
        segy_close(dst);
        close_source();
        throw SaveCanceled();
    };
    if (!tracker.advance(SaveCroppedProgress::Stage::Prepare, 1, 1)) {
        cancelSave();
    }

    std::vector<std::vector<std::vector<float>>> vol(
        static_cast<std::size_t>(n_il_crop),
        std::vector<std::vector<float>>(static_cast<std::size_t>(n_xl_crop)));

    int read_trace = 0;
    for (int il_i = 0; il_i < n_il_crop; ++il_i) {
        for (int xl_i = 0; xl_i < n_xl_crop; ++xl_i) {
            const int tid = traceId(il_lo + il_i, xl_lo + xl_i);
            if (tid < 0 && volume_.empty()) {
                vol[static_cast<std::size_t>(il_i)][static_cast<std::size_t>(xl_i)].assign(
                    static_cast<std::size_t>(n_t_out), 0.f);
            } else {
                const std::vector<float> tr = readTraceAt(il_lo + il_i, xl_lo + xl_i);
                int tr_out_n = 0;
                vol[static_cast<std::size_t>(il_i)][static_cast<std::size_t>(xl_i)] =
                    resampleTrace1D(tr.data(), geom_.n_t, geom_.dt_ms, t_lo, t_hi, dt_out, tr_out_n);
            }
            ++read_trace;
            if (!tracker.advance(SaveCroppedProgress::Stage::ReadTraces, read_trace, n_traces_crop)) {
                cancelSave();
            }
        }
    }

    std::vector<std::vector<std::vector<float>>> vol_out;
    if (spatial_resample) {
        vol_out.assign(static_cast<std::size_t>(n_il_out),
                       std::vector<std::vector<float>>(static_cast<std::size_t>(n_xl_out),
                                                       std::vector<float>(static_cast<std::size_t>(n_t_out), 0.f)));

        std::vector<float> slice2d(static_cast<std::size_t>(n_il_crop) *
                                   static_cast<std::size_t>(n_xl_crop));
        for (int t = 0; t < n_t_out; ++t) {
            for (int il_i = 0; il_i < n_il_crop; ++il_i) {
                for (int xl_i = 0; xl_i < n_xl_crop; ++xl_i) {
                    slice2d[static_cast<std::size_t>(il_i) * static_cast<std::size_t>(n_xl_crop) +
                            static_cast<std::size_t>(xl_i)] =
                        vol[static_cast<std::size_t>(il_i)][static_cast<std::size_t>(xl_i)]
                           [static_cast<std::size_t>(t)];
                }
            }
            int w_sp = 0;
            int h_sp = 0;
            std::vector<int32_t> xl_axis;
            std::vector<int32_t> il_axis;
            const std::vector<float> resampled = resampleSpatial2D(
                slice2d.data(), n_xl_crop, n_il_crop, xl_crop_labels, il_crop_labels,
                xl_crop_labels.front(), xl_crop_labels.back(), il_crop_labels.front(),
                il_crop_labels.back(), d_xl_out, d_il_out, xl_axis, il_axis, w_sp, h_sp);
            for (int il_o = 0; il_o < h_sp; ++il_o) {
                for (int xl_o = 0; xl_o < w_sp; ++xl_o) {
                    vol_out[static_cast<std::size_t>(il_o)][static_cast<std::size_t>(xl_o)]
                           [static_cast<std::size_t>(t)] =
                        resampled[static_cast<std::size_t>(il_o) * static_cast<std::size_t>(w_sp) +
                                  static_cast<std::size_t>(xl_o)];
                }
            }
            if (!tracker.advance(SaveCroppedProgress::Stage::SpatialResample, t + 1, n_t_out)) {
                cancelSave();
            }
        }
    } else {
        vol_out = std::move(vol);
    }

    if (fft_filter != nullptr) {
        int fft_trace = 0;
        for (int il_o = 0; il_o < n_il_out; ++il_o) {
            for (int xl_o = 0; xl_o < n_xl_out; ++xl_o) {
                applyFftFilter1D(vol_out[static_cast<std::size_t>(il_o)][static_cast<std::size_t>(xl_o)],
                                 dt_out, *fft_filter);
                ++fft_trace;
                if (!tracker.advance(SaveCroppedProgress::Stage::Fft1D, fft_trace, n_traces_out)) {
                    cancelSave();
                }
            }
        }
    }

    if (fft_filter2d != nullptr && n_il_out > 0 && n_xl_out > 0) {
        double d_xl = uniformLabelStep(xl_out_labels);
        double d_il = uniformLabelStep(il_out_labels);
        if (d_xl <= 0.0) {
            d_xl = d_xl_out;
        }
        if (d_il <= 0.0) {
            d_il = d_il_out;
        }
        if (d_xl <= 0.0) {
            d_xl = 1.0;
        }
        if (d_il <= 0.0) {
            d_il = 1.0;
        }

        std::vector<float> time_slice(static_cast<std::size_t>(n_xl_out) *
                                      static_cast<std::size_t>(n_il_out));
        for (int t = 0; t < n_t_out; ++t) {
            for (int il_o = 0; il_o < n_il_out; ++il_o) {
                for (int xl_o = 0; xl_o < n_xl_out; ++xl_o) {
                    time_slice[static_cast<std::size_t>(il_o) * static_cast<std::size_t>(n_xl_out) +
                               static_cast<std::size_t>(xl_o)] =
                        vol_out[static_cast<std::size_t>(il_o)][static_cast<std::size_t>(xl_o)]
                               [static_cast<std::size_t>(t)];
                }
            }
            const std::vector<float> filtered =
                filterSlice2D(time_slice, n_xl_out, n_il_out, d_xl, d_il, *fft_filter2d);
            for (int il_o = 0; il_o < n_il_out; ++il_o) {
                for (int xl_o = 0; xl_o < n_xl_out; ++xl_o) {
                    vol_out[static_cast<std::size_t>(il_o)][static_cast<std::size_t>(xl_o)]
                           [static_cast<std::size_t>(t)] =
                        filtered[static_cast<std::size_t>(il_o) * static_cast<std::size_t>(n_xl_out) +
                                 static_cast<std::size_t>(xl_o)];
                }
            }
            if (!tracker.advance(SaveCroppedProgress::Stage::Fft2D, t + 1, n_t_out)) {
                cancelSave();
            }
        }
    }

    char binary[kBinaryHeaderSize];
    std::memcpy(binary, headers_.binary, kBinaryHeaderSize);
    const int interval_us = static_cast<int>(std::lround(static_cast<double>(dt_out) * 1000.0));
    if (segy_set_binfield_int(binary, SEGY_BIN_SAMPLES, n_t_out) != SEGY_OK ||
        segy_set_binfield_int(binary, SEGY_BIN_SAMPLES_ORIG, n_t_out) != SEGY_OK ||
        segy_set_binfield_int(binary, SEGY_BIN_INTERVAL, interval_us) != SEGY_OK ||
        segy_set_binfield_int(binary, SEGY_BIN_FORMAT, SEGY_IEEE_FLOAT_4_BYTE) != SEGY_OK) {
        segy_close(dst);
        close_source();
        throw std::runtime_error("SegyCube::saveCropped: failed to update binary header");
    }

    if (segy_write_binheader(dst, binary) != SEGY_OK) {
        segy_close(dst);
        close_source();
        throw std::runtime_error("SegyCube::saveCropped: failed to write binary header");
    }

    unsigned long long trace0 = 0;
    if (segy_trace0(binary, &trace0, ext_text_headers) != SEGY_OK) {
        segy_close(dst);
        close_source();
        throw std::runtime_error("SegyCube::saveCropped: failed to compute trace0");
    }

    int traceheader_count = 1;
    if (segy_traceheaders(binary, &traceheader_count) != SEGY_OK) {
        segy_close(dst);
        close_source();
        throw std::runtime_error("SegyCube::saveCropped: failed to read trace header count");
    }

    dst->metadata.format = SEGY_IEEE_FLOAT_4_BYTE;
    dst->metadata.elemsize = segy_formatsize(SEGY_IEEE_FLOAT_4_BYTE);
    dst->metadata.samplecount = n_t_out;
    dst->metadata.trace_bsize = n_t_out * dst->metadata.elemsize;
    dst->metadata.trace0 = static_cast<long long>(trace0);
    dst->metadata.traceheader_count = traceheader_count;
    dst->metadata.ext_textheader_count = ext_text_headers;

    std::vector<char> trace_hdr(kTraceHeaderSize);
    std::vector<char> sample_buf(static_cast<std::size_t>(n_t_out) *
                                 static_cast<std::size_t>(dst->metadata.elemsize));

    const int template_il = il_lo;
    const int template_xl = xl_lo;
    const int template_tid = traceId(template_il, template_xl);
    if (template_tid < 0 ||
        segy_read_standard_traceheader(src, template_tid, trace_hdr.data()) != SEGY_OK) {
        segy_close(dst);
        close_source();
        throw std::runtime_error("SegyCube::saveCropped: failed to read template trace header");
    }

    int out_trace = 0;
    for (int il_o = 0; il_o < n_il_out; ++il_o) {
        for (int xl_o = 0; xl_o < n_xl_out; ++xl_o) {
            if (segy_set_tracefield_int(trace_hdr.data(), SEGY_TR_INLINE,
                                          il_out_labels[static_cast<std::size_t>(il_o)]) != SEGY_OK ||
                segy_set_tracefield_int(trace_hdr.data(), SEGY_TR_CROSSLINE,
                                          xl_out_labels[static_cast<std::size_t>(xl_o)]) != SEGY_OK ||
                segy_set_tracefield_int(trace_hdr.data(), SEGY_TR_SAMPLE_COUNT, n_t_out) != SEGY_OK) {
                segy_close(dst);
                close_source();
                throw std::runtime_error("SegyCube::saveCropped: failed to update trace header fields");
            }

            int delay_ms = 0;
            if (segy_get_tracefield_int(trace_hdr.data(), SEGY_TR_DELAY_REC_TIME, &delay_ms) == SEGY_OK) {
                const int added_ms =
                    static_cast<int>(std::lround(static_cast<double>(t_lo) * geom_.dt_ms));
                (void)segy_set_tracefield_int(trace_hdr.data(), SEGY_TR_DELAY_REC_TIME, delay_ms + added_ms);
            }

            const auto& samples = vol_out[static_cast<std::size_t>(il_o)][static_cast<std::size_t>(xl_o)];
            if (static_cast<int>(samples.size()) != n_t_out) {
                segy_close(dst);
                close_source();
                throw std::runtime_error("SegyCube::saveCropped: internal sample count mismatch");
            }
            std::memcpy(sample_buf.data(), samples.data(),
                        static_cast<std::size_t>(n_t_out) * sizeof(float));

            if (segy_from_native(SEGY_IEEE_FLOAT_4_BYTE, static_cast<long long>(n_t_out),
                                 sample_buf.data()) != SEGY_OK) {
                segy_close(dst);
                close_source();
                throw std::runtime_error("SegyCube::saveCropped: failed to encode IEEE samples");
            }

            if (segy_write_standard_traceheader(dst, out_trace, trace_hdr.data()) != SEGY_OK ||
                segy_writetrace(dst, out_trace, sample_buf.data()) != SEGY_OK) {
                segy_close(dst);
                close_source();
                throw std::runtime_error("SegyCube::saveCropped: failed to write trace " +
                                         std::to_string(out_trace));
            }
            ++out_trace;
            if (!tracker.advance(SaveCroppedProgress::Stage::WriteSegy, out_trace, n_traces_out)) {
                cancelSave();
            }
        }
    }

    tracker.overall_current = tracker.overall_total;
    (void)tracker.report(SaveCroppedProgress::Stage::WriteSegy, n_traces_out, n_traces_out);

    segy_flush(dst);
    segy_close(dst);
    close_source();
}

namespace {

MinMaxMedian minMaxMedianFromValues(std::vector<double> values) {
    MinMaxMedian out;
    if (values.empty()) {
        return out;
    }
    std::sort(values.begin(), values.end());
    out.min_val = values.front();
    out.max_val = values.back();
    const std::size_t n = values.size();
    if (n % 2 == 1) {
        out.median_val = values[n / 2];
    } else {
        out.median_val = 0.5 * (values[n / 2 - 1] + values[n / 2]);
    }
    return out;
}

MinMaxMedian minMaxMedianFromLabels(const std::vector<int32_t>& labels) {
    std::vector<double> values;
    values.reserve(labels.size());
    for (int32_t v : labels) {
        values.push_back(static_cast<double>(v));
    }
    return minMaxMedianFromValues(std::move(values));
}

struct PlaneFit {
    double c0 = 0.0;
    double c_il = 0.0;
    double c_xl = 0.0;
    bool ok = false;
};

PlaneFit fitCoordinatePlane(const std::vector<double>& il,
                            const std::vector<double>& xl,
                            const std::vector<double>& z) {
    PlaneFit fit;
    const std::size_t n = il.size();
    if (n < 3 || xl.size() != n || z.size() != n) {
        return fit;
    }

    double s1 = static_cast<double>(n);
    double s_il = 0.0;
    double s_xl = 0.0;
    double s_il2 = 0.0;
    double s_xl2 = 0.0;
    double s_ilxl = 0.0;
    double s_z = 0.0;
    double s_ilz = 0.0;
    double s_xlz = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double v_il = il[i];
        const double v_xl = xl[i];
        const double v_z = z[i];
        s_il += v_il;
        s_xl += v_xl;
        s_il2 += v_il * v_il;
        s_xl2 += v_xl * v_xl;
        s_ilxl += v_il * v_xl;
        s_z += v_z;
        s_ilz += v_il * v_z;
        s_xlz += v_xl * v_z;
    }

    const double det =
        s1 * (s_il2 * s_xl2 - s_ilxl * s_ilxl) - s_il * (s_il * s_xl2 - s_xl * s_ilxl) +
        s_xl * (s_il * s_ilxl - s_il2 * s_xl);
    if (std::abs(det) < 1e-12) {
        return fit;
    }

    fit.c0 = (s_z * (s_il2 * s_xl2 - s_ilxl * s_ilxl) -
              s_il * (s_ilz * s_xl2 - s_xlz * s_ilxl) + s_xl * (s_ilz * s_ilxl - s_il2 * s_xlz)) /
             det;
    fit.c_il = (s1 * (s_ilz * s_xl2 - s_xlz * s_ilxl) - s_z * (s_il * s_xl2 - s_xl * s_ilxl) +
                s_xl * (s_il * s_xlz - s_ilz * s_xl)) /
               det;
    fit.c_xl = (s1 * (s_il2 * s_xlz - s_ilxl * s_ilz) - s_il * (s_il * s_xlz - s_ilz * s_xl) +
                s_z * (s_il * s_ilxl - s_il2 * s_xl)) /
               det;
    fit.ok = true;
    return fit;
}

struct LineFit {
    double intercept = 0.0;
    double slope = 0.0;
    bool ok = false;
};

LineFit fitLine(const std::vector<double>& x, const std::vector<double>& y) {
    LineFit fit;
    const std::size_t n = x.size();
    if (n < 2 || y.size() != n) {
        return fit;
    }
    double mx = 0.0;
    double my = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        mx += x[i];
        my += y[i];
    }
    mx /= static_cast<double>(n);
    my /= static_cast<double>(n);

    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double dx = x[i] - mx;
        num += dx * (y[i] - my);
        den += dx * dx;
    }
    if (std::abs(den) < 1e-12) {
        return fit;
    }
    fit.slope = num / den;
    fit.intercept = my - fit.slope * mx;
    fit.ok = true;
    return fit;
}

double normalizeAzimuthDeg(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0) {
        deg += 360.0;
    }
    return deg;
}

}  // namespace

SurveyCoordinateStats SegyCube::surveyCoordinates() const {
    SurveyCoordinateStats stats;
    if (!loaded_ || geom_.n_il <= 0 || geom_.n_xl <= 0) {
        return stats;
    }

    stats.inline_stats = minMaxMedianFromLabels(inlines_);
    stats.crossline_stats = minMaxMedianFromLabels(crosslines_);

    std::vector<double> cdp_x_vals;
    std::vector<double> cdp_y_vals;
    std::vector<double> reg_il;
    std::vector<double> reg_xl;
    std::vector<double> reg_cdp_x;
    std::vector<double> reg_cdp_y;

    bool rectangular = true;
    const std::size_t grid_size =
        static_cast<std::size_t>(geom_.n_il) * static_cast<std::size_t>(geom_.n_xl);

    for (int il_idx = 0; il_idx < geom_.n_il; ++il_idx) {
        for (int xl_idx = 0; xl_idx < geom_.n_xl; ++xl_idx) {
            const std::size_t flat =
                static_cast<std::size_t>(il_idx) * static_cast<std::size_t>(geom_.n_xl) +
                static_cast<std::size_t>(xl_idx);
            if (flat >= grid_size || trace_ids_[flat] < 0) {
                rectangular = false;
                continue;
            }
            const double cx = cdp_x_[flat];
            const double cy = cdp_y_[flat];
            if (!std::isfinite(cx) || !std::isfinite(cy)) {
                rectangular = false;
                continue;
            }
            cdp_x_vals.push_back(cx);
            cdp_y_vals.push_back(cy);
            stats.cdp_points.push_back({cx, cy});
            reg_il.push_back(static_cast<double>(inlineLabel(il_idx)));
            reg_xl.push_back(static_cast<double>(crosslineLabel(xl_idx)));
            reg_cdp_x.push_back(cx);
            reg_cdp_y.push_back(cy);
        }
    }

    stats.cdp_x_stats = minMaxMedianFromValues(cdp_x_vals);
    stats.cdp_y_stats = minMaxMedianFromValues(cdp_y_vals);

    const int mid_il_idx = geom_.n_il / 2;
    std::vector<double> mid_cdp_x;
    std::vector<double> mid_cdp_y;
    for (int xl_idx = 0; xl_idx < geom_.n_xl; ++xl_idx) {
        const std::size_t flat =
            static_cast<std::size_t>(mid_il_idx) * static_cast<std::size_t>(geom_.n_xl) +
            static_cast<std::size_t>(xl_idx);
        if (flat >= grid_size) {
            continue;
        }
        const double cx = cdp_x_[flat];
        const double cy = cdp_y_[flat];
        if (!std::isfinite(cx) || !std::isfinite(cy)) {
            continue;
        }
        mid_cdp_x.push_back(cx);
        mid_cdp_y.push_back(cy);
    }

    const LineFit inline_fit = fitLine(mid_cdp_x, mid_cdp_y);
    if (inline_fit.ok) {
        const double az_rad = std::atan2(1.0, inline_fit.slope);
        stats.inline_azimuth_deg = normalizeAzimuthDeg(az_rad * 180.0 / M_PI);
    }

    const PlaneFit plane_x = fitCoordinatePlane(reg_il, reg_xl, reg_cdp_x);
    const PlaneFit plane_y = fitCoordinatePlane(reg_il, reg_xl, reg_cdp_y);

    const int il_lo = 0;
    const int il_hi = geom_.n_il - 1;
    const int xl_lo = 0;
    const int xl_hi = geom_.n_xl - 1;
    const struct {
        int il_idx;
        int xl_idx;
    } corner_idx[] = {
        {il_lo, xl_lo},
        {il_lo, xl_hi},
        {il_hi, xl_hi},
        {il_hi, xl_lo},
    };

    stats.corners.reserve(4);
    for (int i = 0; i < 4; ++i) {
        const int il_idx = corner_idx[i].il_idx;
        const int xl_idx = corner_idx[i].xl_idx;
        const int32_t il_label = inlineLabel(il_idx);
        const int32_t xl_label = crosslineLabel(xl_idx);

        SurveyCornerPoint corner;
        corner.point_num = i + 1;
        corner.inline_label = il_label;
        corner.crossline_label = xl_label;

        const std::size_t flat =
            static_cast<std::size_t>(il_idx) * static_cast<std::size_t>(geom_.n_xl) +
            static_cast<std::size_t>(xl_idx);
        const bool has_trace =
            flat < grid_size && trace_ids_[flat] >= 0 && std::isfinite(cdp_x_[flat]) &&
            std::isfinite(cdp_y_[flat]);

        if (rectangular && has_trace) {
            corner.cdp_x = cdp_x_[flat];
            corner.cdp_y = cdp_y_[flat];
        } else if (plane_x.ok && plane_y.ok) {
            const double il_v = static_cast<double>(il_label);
            const double xl_v = static_cast<double>(xl_label);
            corner.cdp_x = plane_x.c0 + plane_x.c_il * il_v + plane_x.c_xl * xl_v;
            corner.cdp_y = plane_y.c0 + plane_y.c_il * il_v + plane_y.c_xl * xl_v;
        } else if (has_trace) {
            corner.cdp_x = cdp_x_[flat];
            corner.cdp_y = cdp_y_[flat];
        }

        stats.corners.push_back(corner);
    }

    return stats;
}

const char* segySampleFormatName(int format_code) {
    switch (format_code) {
    case SEGY_IBM_FLOAT_4_BYTE:
        return "IBM float 32-bit";
    case SEGY_SIGNED_INTEGER_4_BYTE:
        return "32-bit integer";
    case SEGY_SIGNED_SHORT_2_BYTE:
        return "16-bit integer";
    case SEGY_FIXED_POINT_WITH_GAIN_4_BYTE:
        return "32-bit fixed point";
    case SEGY_IEEE_FLOAT_4_BYTE:
        return "IEEE float 32-bit";
    case SEGY_IEEE_FLOAT_8_BYTE:
        return "IEEE float 64-bit";
    case SEGY_SIGNED_INTEGER_8_BYTE:
        return "64-bit integer";
    case SEGY_UNSIGNED_INTEGER_8_BYTE:
        return "64-bit unsigned integer";
    case SEGY_UNSIGNED_INTEGER_4_BYTE:
        return "32-bit unsigned integer";
    case SEGY_UNSIGNED_SHORT_2_BYTE:
        return "16-bit unsigned integer";
    default:
        return "unknown";
    }
}

}  // namespace kubik
