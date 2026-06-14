#include "kubik/Resample.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kubik {

namespace {

constexpr int kLanczosA = 4;

double sincPi(double x) {
    if (std::abs(x) < 1e-12) {
        return 1.0;
    }
    return std::sin(M_PI * x) / (M_PI * x);
}

double lanczos(double x, int a) {
    if (std::abs(x) >= static_cast<double>(a)) {
        return 0.0;
    }
    return sincPi(x) * sincPi(x / static_cast<double>(a));
}

double labelToFracIndex(const std::vector<int32_t>& labels, double label) {
    if (labels.empty()) {
        return 0.0;
    }
    if (labels.size() == 1) {
        return 0.0;
    }
    if (label <= static_cast<double>(labels.front())) {
        const double d0 = static_cast<double>(labels[1] - labels[0]);
        if (std::abs(d0) < 1e-9) {
            return 0.0;
        }
        return (label - static_cast<double>(labels.front())) / d0;
    }
    if (label >= static_cast<double>(labels.back())) {
        const std::size_t n = labels.size();
        const double d1 = static_cast<double>(labels[n - 1] - labels[n - 2]);
        if (std::abs(d1) < 1e-9) {
            return static_cast<double>(n - 1);
        }
        return static_cast<double>(n - 1) +
               (label - static_cast<double>(labels.back())) / d1;
    }
    const auto it = std::upper_bound(labels.begin(), labels.end(), static_cast<int32_t>(std::floor(label)));
    const std::size_t i1 = static_cast<std::size_t>(std::max<std::ptrdiff_t>(1, it - labels.begin()));
    const std::size_t i0 = i1 - 1;
    const double l0 = static_cast<double>(labels[i0]);
    const double l1 = static_cast<double>(labels[i1]);
    const double t = (label - l0) / (l1 - l0);
    return static_cast<double>(i0) + t;
}

float interpolate1D(const float* data, int n, double pos, double aa_scale) {
    if (n <= 0) {
        return 0.f;
    }
    if (n == 1) {
        return data[0];
    }
    const int center = static_cast<int>(std::floor(pos));
    double sum = 0.0;
    double wsum = 0.0;
    for (int k = center - kLanczosA + 1; k <= center + kLanczosA; ++k) {
        if (k < 0 || k >= n) {
            continue;
        }
        const double w = lanczos((pos - static_cast<double>(k)) * aa_scale, kLanczosA);
        sum += static_cast<double>(data[k]) * w;
        wsum += w;
    }
    if (wsum <= 0.0) {
        const int idx = std::clamp(center, 0, n - 1);
        return data[static_cast<std::size_t>(idx)];
    }
    return static_cast<float>(sum / wsum);
}

float interpolate2D(const float* data, int w, int h, double fx, double fy, double aa_x, double aa_y) {
    if (w <= 0 || h <= 0) {
        return 0.f;
    }
    const int cx = static_cast<int>(std::floor(fx));
    const int cy = static_cast<int>(std::floor(fy));
    double sum = 0.0;
    double wsum = 0.0;
    for (int dy = -kLanczosA + 1; dy <= kLanczosA; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= h) {
            continue;
        }
        const double wy = lanczos((fy - static_cast<double>(y)) * aa_y, kLanczosA);
        for (int dx = -kLanczosA + 1; dx <= kLanczosA; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= w) {
                continue;
            }
            const double wx = lanczos((fx - static_cast<double>(x)) * aa_x, kLanczosA);
            const double wgt = wx * wy;
            sum += static_cast<double>(data[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                              static_cast<std::size_t>(x)]) *
                    wgt;
            wsum += wgt;
        }
    }
    if (wsum <= 0.0) {
        const int x = std::clamp(cx, 0, w - 1);
        const int y = std::clamp(cy, 0, h - 1);
        return data[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
    }
    return static_cast<float>(sum / wsum);
}

std::vector<float> extractCrop2D(const float* data, int w, int h, int x0, int x1, int y0, int y1, int& w_out,
                                 int& h_out) {
    x0 = std::clamp(x0, 0, std::max(0, w - 1));
    x1 = std::clamp(x1, 0, std::max(0, w - 1));
    y0 = std::clamp(y0, 0, std::max(0, h - 1));
    y1 = std::clamp(y1, 0, std::max(0, h - 1));
    if (x0 > x1) {
        std::swap(x0, x1);
    }
    if (y0 > y1) {
        std::swap(y0, y1);
    }
    w_out = x1 - x0 + 1;
    h_out = y1 - y0 + 1;
    std::vector<float> out(static_cast<std::size_t>(w_out) * static_cast<std::size_t>(h_out));
    for (int y = 0; y < h_out; ++y) {
        for (int x = 0; x < w_out; ++x) {
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_out) + static_cast<std::size_t>(x)] =
                data[static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(w) +
                     static_cast<std::size_t>(x0 + x)];
        }
    }
    return out;
}

}  // namespace

float uniformLabelStep(const std::vector<int32_t>& labels) {
    if (labels.size() < 2) {
        return 0.f;
    }
    double sum = 0.0;
    for (std::size_t i = 1; i < labels.size(); ++i) {
        sum += static_cast<double>(labels[i] - labels[i - 1]);
    }
    const double avg = sum / static_cast<double>(labels.size() - 1);
    if (std::abs(avg - std::round(avg)) > 1e-6) {
        return 0.f;
    }
    for (std::size_t i = 1; i < labels.size(); ++i) {
        const double d = static_cast<double>(labels[i] - labels[i - 1]);
        if (std::abs(d - avg) > 1e-6) {
            return 0.f;
        }
    }
    return static_cast<float>(avg);
}

std::vector<int32_t> buildLabelAxis(int32_t min_label, int32_t max_label, float step) {
    std::vector<int32_t> out;
    if (step <= 0.f || max_label < min_label) {
        return out;
    }
    for (double v = static_cast<double>(min_label); v <= static_cast<double>(max_label) + 1e-6;
         v += static_cast<double>(step)) {
        out.push_back(static_cast<int32_t>(std::lround(v)));
    }
    if (out.empty() || out.back() != max_label) {
        if (out.empty() || out.back() < max_label) {
            out.push_back(max_label);
        }
    }
    return out;
}

std::vector<int32_t> buildTimeMsAxis(int t_min_idx, int t_max_idx, float dt_in_ms, float dt_out_ms) {
    std::vector<int32_t> out;
    if (t_max_idx < t_min_idx || dt_in_ms <= 0.f) {
        return out;
    }
  const float t0_ms = static_cast<float>(t_min_idx) * dt_in_ms;
    const float t1_ms = static_cast<float>(t_max_idx) * dt_in_ms;
    const float use_dt = (dt_out_ms > 0.f) ? dt_out_ms : dt_in_ms;
    for (float t = t0_ms; t <= t1_ms + 1e-4f; t += use_dt) {
        out.push_back(static_cast<int32_t>(std::lround(t)));
    }
    return out;
}

bool needsTimeResample(float dt_in_ms, float dt_out_ms) {
    if (dt_out_ms <= 0.f || dt_in_ms <= 0.f) {
        return false;
    }
    return std::abs(dt_out_ms - dt_in_ms) > 1e-4f;
}

bool needsSpatialResample(float d_native, float d_out) {
    if (d_out <= 0.f || d_native <= 0.f) {
        return false;
    }
    return std::abs(d_out - d_native) > 1e-4f;
}

std::vector<float> resampleTrace1D(const float* samples, int n_in, float dt_in_ms, int t_min_idx,
                                   int t_max_idx, float dt_out_ms, int& n_out) {
    n_out = 0;
    if (!samples || n_in <= 0 || dt_in_ms <= 0.f || t_max_idx < t_min_idx) {
        return {};
    }
    t_min_idx = std::clamp(t_min_idx, 0, n_in - 1);
    t_max_idx = std::clamp(t_max_idx, 0, n_in - 1);
    if (t_min_idx > t_max_idx) {
        return {};
    }

    const float use_dt = (dt_out_ms > 0.f) ? dt_out_ms : dt_in_ms;
    if (!needsTimeResample(dt_in_ms, use_dt)) {
        n_out = t_max_idx - t_min_idx + 1;
        std::vector<float> out(static_cast<std::size_t>(n_out));
        for (int i = 0; i < n_out; ++i) {
            out[static_cast<std::size_t>(i)] = samples[static_cast<std::size_t>(t_min_idx + i)];
        }
        return out;
    }

    const double t0_ms = static_cast<double>(t_min_idx) * static_cast<double>(dt_in_ms);
    const double t1_ms = static_cast<double>(t_max_idx) * static_cast<double>(dt_in_ms);
    n_out = static_cast<int>(std::floor((t1_ms - t0_ms) / static_cast<double>(use_dt))) + 1;
    if (n_out <= 0) {
        return {};
    }

    const double aa_scale = static_cast<double>(dt_in_ms) / static_cast<double>(use_dt);
    std::vector<float> out(static_cast<std::size_t>(n_out));
    for (int j = 0; j < n_out; ++j) {
        const double t_ms = t0_ms + static_cast<double>(j) * static_cast<double>(use_dt);
        const double pos = t_ms / static_cast<double>(dt_in_ms);
        out[static_cast<std::size_t>(j)] = interpolate1D(samples, n_in, pos, aa_scale);
    }
    return out;
}

std::vector<float> resampleSpatial2D(const float* data, int w_in, int h_in,
                                     const std::vector<int32_t>& x_labels_in,
                                     const std::vector<int32_t>& y_labels_in, int32_t x_min_label,
                                     int32_t x_max_label, int32_t y_min_label, int32_t y_max_label,
                                     float dx_out, float dy_out, std::vector<int32_t>& x_labels_out,
                                     std::vector<int32_t>& y_labels_out, int& w_out, int& h_out) {
    w_out = 0;
    h_out = 0;
    if (!data || w_in <= 0 || h_in <= 0 || static_cast<int>(x_labels_in.size()) != w_in ||
        static_cast<int>(y_labels_in.size()) != h_in) {
        return {};
    }

    const float d_in_x = uniformLabelStep(x_labels_in);
    const float d_in_y = uniformLabelStep(y_labels_in);
    const float use_dx = (dx_out > 0.f) ? dx_out : (d_in_x > 0.f ? d_in_x : 1.f);
    const float use_dy = (dy_out > 0.f) ? dy_out : (d_in_y > 0.f ? d_in_y : 1.f);

    if (!needsSpatialResample(d_in_x, use_dx) && !needsSpatialResample(d_in_y, use_dy)) {
        w_out = w_in;
        h_out = h_in;
        x_labels_out = x_labels_in;
        y_labels_out = y_labels_in;
        return std::vector<float>(data, data + static_cast<std::size_t>(w_in) * static_cast<std::size_t>(h_in));
    }

    x_labels_out = buildLabelAxis(x_min_label, x_max_label, use_dx);
    y_labels_out = buildLabelAxis(y_min_label, y_max_label, use_dy);
    w_out = static_cast<int>(x_labels_out.size());
    h_out = static_cast<int>(y_labels_out.size());
    if (w_out <= 0 || h_out <= 0) {
        return {};
    }

    const double aa_x = (d_in_x > 0.f) ? static_cast<double>(d_in_x / use_dx) : 1.0;
    const double aa_y = (d_in_y > 0.f) ? static_cast<double>(d_in_y / use_dy) : 1.0;

    std::vector<float> out(static_cast<std::size_t>(w_out) * static_cast<std::size_t>(h_out));
    for (int iy = 0; iy < h_out; ++iy) {
        const double y_label = static_cast<double>(y_labels_out[static_cast<std::size_t>(iy)]);
        const double fy = labelToFracIndex(y_labels_in, y_label);
        for (int ix = 0; ix < w_out; ++ix) {
            const double x_label = static_cast<double>(x_labels_out[static_cast<std::size_t>(ix)]);
            const double fx = labelToFracIndex(x_labels_in, x_label);
            out[static_cast<std::size_t>(iy) * static_cast<std::size_t>(w_out) + static_cast<std::size_t>(ix)] =
                interpolate2D(data, w_in, h_in, fx, fy, aa_x, aa_y);
        }
    }
    return out;
}

}  // namespace kubik
