#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace kubik {

inline float amplitudePercentile(const std::vector<float>& sorted, float percentile) {
    if (sorted.empty()) {
        return 0.f;
    }
    percentile = std::clamp(percentile, 0.f, 100.f);
    const float pos = percentile / 100.f * static_cast<float>(sorted.size() - 1);
    const std::size_t i0 = static_cast<std::size_t>(pos);
    const std::size_t i1 = std::min(i0 + 1, sorted.size() - 1);
    const float t = pos - static_cast<float>(i0);
    return (1.f - t) * sorted[i0] + t * sorted[i1];
}

/// clip_percent — ширина центральной части распределения (%).
/// Симметрично отсекается (100−clip)/2 % с каждого хвоста: clip=99 → [p0.5, p99.5], clip=1 → [p49.5, p50.5].
inline void amplitudeClipRangeFromSorted(const std::vector<float>& sorted, float clip_percent,
                                         float& out_vmin, float& out_vmax) {
    if (sorted.empty()) {
        out_vmin = 0.f;
        out_vmax = 1.f;
        return;
    }
    clip_percent = std::clamp(clip_percent, 1.f, 100.f);
    const float tail = (100.f - clip_percent) * 0.5f;
    out_vmin = amplitudePercentile(sorted, tail);
    out_vmax = amplitudePercentile(sorted, 100.f - tail);
    if (out_vmax < out_vmin) {
        std::swap(out_vmin, out_vmax);
    }
}

inline void amplitudeClipRange(const std::vector<float>& data, float clip_percent, float& out_vmin,
                               float& out_vmax) {
    if (data.empty()) {
        out_vmin = 0.f;
        out_vmax = 1.f;
        return;
    }
    std::vector<float> sorted(data.begin(), data.end());
    std::sort(sorted.begin(), sorted.end());
    amplitudeClipRangeFromSorted(sorted, clip_percent, out_vmin, out_vmax);
}

}  // namespace kubik
