#include "kubik/FftFilter.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <kfr/dft.hpp>

namespace kubik {

namespace {

namespace k = kfr;

void fftshift2d(std::vector<k::complex<double>>& data, int w, int h) {
    if (w <= 1 && h <= 1) {
        return;
    }
    if (w > 1) {
        const int half_w = w / 2;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < half_w; ++x) {
                std::swap(data[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                  static_cast<std::size_t>(x)],
                          data[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                  static_cast<std::size_t>(x + half_w)]);
            }
        }
    }
    if (h > 1) {
        const int half_h = h / 2;
        for (int y = 0; y < half_h; ++y) {
            for (int x = 0; x < w; ++x) {
                std::swap(data[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                  static_cast<std::size_t>(x)],
                          data[static_cast<std::size_t>(y + half_h) * static_cast<std::size_t>(w) +
                                  static_cast<std::size_t>(x)]);
            }
        }
    }
}

void ifftshift2d(std::vector<k::complex<double>>& data, int w, int h) {
    fftshift2d(data, w, h);
}

void buildKAxes(int w, int h, double d_xl, double d_il, std::vector<double>& k_xl,
                std::vector<double>& k_il) {
    k_xl.resize(static_cast<std::size_t>(w));
    k_il.resize(static_cast<std::size_t>(h));
    const double dx = std::max(d_xl, 1e-9);
    const double dy = std::max(d_il, 1e-9);
    for (int x = 0; x < w; ++x) {
        const int bin = x - w / 2;
        k_xl[static_cast<std::size_t>(x)] = static_cast<double>(bin) / (static_cast<double>(w) * dx);
    }
    for (int y = 0; y < h; ++y) {
        const int bin = y - h / 2;
        k_il[static_cast<std::size_t>(y)] = static_cast<double>(bin) / (static_cast<double>(h) * dy);
    }
}

struct Fft2DPlan {
    int w = 0;
    int h = 0;
    std::optional<k::dft_plan_md<double, 2>> plan;
    k::univector<k::u8> temp;

    void ensure(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        if (width == w && height == h) {
            return;
        }
        w = width;
        h = height;
        plan.emplace(k::shape<2>{static_cast<k::index_t>(h), static_cast<k::index_t>(w)});
        temp.resize(std::max(plan->temp_size, std::size_t(1)));
    }
};

Fft2DPlan& fft2DPlan() {
    thread_local Fft2DPlan cache;
    return cache;
}

bool forward2D(const std::vector<float>& slice, int w, int h,
               std::vector<k::complex<double>>& freq) {
    if (w <= 0 || h <= 0 || static_cast<int>(slice.size()) < w * h) {
        return false;
    }
    Fft2DPlan& cache = fft2DPlan();
    cache.ensure(w, h);
    if (!cache.plan) {
        return false;
    }

    std::vector<k::complex<double>> in(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    freq.resize(in.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            in[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                static_cast<double>(
                    slice[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]);
        }
    }

    cache.plan->execute(freq.data(), in.data(), cache.temp.data(), false);
    fftshift2d(freq, w, h);
    return true;
}

bool inverse2D(std::vector<k::complex<double>>& freq, int w, int h, std::vector<float>& out) {
    if (w <= 0 || h <= 0 || static_cast<int>(freq.size()) < w * h) {
        return false;
    }
    Fft2DPlan& cache = fft2DPlan();
    cache.ensure(w, h);
    if (!cache.plan) {
        return false;
    }

    ifftshift2d(freq, w, h);
    std::vector<k::complex<double>> spatial(freq.size());
    cache.plan->execute(spatial.data(), freq.data(), cache.temp.data(), true);

    out.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    const double norm = 1.0 / (static_cast<double>(w) * static_cast<double>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto& c =
                spatial[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                static_cast<float>(c.real() * norm);
        }
    }
    return true;
}

/// Маска вдоль оси: |k|<k_pass — 1; [k_pass,k_cut) — вырезание со сглаживанием k_smooth на краях.
double axisFootprintGain(double abs_k, double k_cut, double k_pass, double k_smooth) {
    if (k_cut <= k_pass) {
        return 1.0;
    }
    if (abs_k >= k_cut || abs_k < k_pass) {
        return 1.0;
    }

    k_smooth = std::max(0.0, k_smooth);
    const double band = k_cut - k_pass;
    const double ramp_end = k_pass + k_smooth;
    const double ramp_start = k_cut - k_smooth;

    if (k_smooth <= 0.0 || ramp_end >= ramp_start) {
        const double t = (abs_k - k_pass) / band;
        return 0.5 * (1.0 + std::cos(M_PI * t));
    }
    if (abs_k < ramp_end) {
        const double t = (abs_k - k_pass) / k_smooth;
        return 0.5 * (1.0 + std::cos(M_PI * t));
    }
    if (abs_k < ramp_start) {
        return 0.0;
    }
    const double t = (k_cut - abs_k) / k_smooth;
    return 0.5 * (1.0 - std::cos(M_PI * t));
}

}  // namespace

double footprintGain2D(double k_il, double k_xl, const FftFilter2DParams& params) {
    double gain = 1.0;
    switch (params.type) {
    case FftFilter2DType::FootprintIl:
        gain = axisFootprintGain(std::abs(k_il), params.k_cut_il, params.k_pass, params.k_smooth);
        break;
    case FftFilter2DType::FootprintXl:
        gain = axisFootprintGain(std::abs(k_xl), params.k_cut_xl, params.k_pass, params.k_smooth);
        break;
    case FftFilter2DType::FootprintIlXl:
        gain = axisFootprintGain(std::abs(k_il), params.k_cut_il, params.k_pass, params.k_smooth) *
               axisFootprintGain(std::abs(k_xl), params.k_cut_xl, params.k_pass, params.k_smooth);
        break;
    }
    return gain;
}

Spectrum2D computeSpectrum2D(const std::vector<float>& slice, int w, int h, double d_xl, double d_il) {
    Spectrum2D spec;
    spec.w = w;
    spec.h = h;
    if (w <= 0 || h <= 0) {
        return spec;
    }

    std::vector<k::complex<double>> freq;
    if (!forward2D(slice, w, h, freq)) {
        return spec;
    }

    buildKAxes(w, h, d_xl, d_il, spec.k_xl, spec.k_il);
    spec.amps.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto& c =
                freq[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
            spec.amps[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                std::abs(c);
        }
    }
    return spec;
}

std::vector<float> filterSlice2D(const std::vector<float>& slice, int w, int h, double d_xl, double d_il,
                                   const FftFilter2DParams& params) {
    if (w <= 0 || h <= 0 || slice.empty()) {
        return slice;
    }

    std::vector<k::complex<double>> freq;
    if (!forward2D(slice, w, h, freq)) {
        return slice;
    }

    std::vector<double> k_xl;
    std::vector<double> k_il;
    buildKAxes(w, h, d_xl, d_il, k_xl, k_il);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double gain =
                footprintGain2D(k_il[static_cast<std::size_t>(y)], k_xl[static_cast<std::size_t>(x)], params);
            auto& c =
                freq[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
            c *= gain;
        }
    }

    std::vector<float> out;
    if (!inverse2D(freq, w, h, out)) {
        return slice;
    }
    return out;
}

std::vector<float> filterSliceRegion2D(const std::vector<float>& slice, int w, int h, int h0, int h1,
                                       int v0, int v1, double d_xl, double d_il,
                                       const FftFilter2DParams& params) {
    std::vector<float> out = slice;
    if (out.empty() || w <= 0 || h <= 0) {
        return out;
    }

    h0 = std::clamp(h0, 0, w - 1);
    h1 = std::clamp(h1, 0, w - 1);
    v0 = std::clamp(v0, 0, h - 1);
    v1 = std::clamp(v1, 0, h - 1);
    if (h0 > h1) {
        std::swap(h0, h1);
    }
    if (v0 > v1) {
        std::swap(v0, v1);
    }

    const int rw = h1 - h0 + 1;
    const int rh = v1 - v0 + 1;
    std::vector<float> region(static_cast<std::size_t>(rw) * static_cast<std::size_t>(rh));
    for (int v = 0; v < rh; ++v) {
        for (int x = 0; x < rw; ++x) {
            region[static_cast<std::size_t>(v) * static_cast<std::size_t>(rw) + static_cast<std::size_t>(x)] =
                out[static_cast<std::size_t>(v0 + v) * static_cast<std::size_t>(w) + static_cast<std::size_t>(h0 + x)];
        }
    }

    const std::vector<float> filtered = filterSlice2D(region, rw, rh, d_xl, d_il, params);
    for (int v = 0; v < rh; ++v) {
        for (int x = 0; x < rw; ++x) {
            out[static_cast<std::size_t>(v0 + v) * static_cast<std::size_t>(w) + static_cast<std::size_t>(h0 + x)] =
                filtered[static_cast<std::size_t>(v) * static_cast<std::size_t>(rw) + static_cast<std::size_t>(x)];
        }
    }
    return out;
}

}  // namespace kubik
