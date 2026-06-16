#include "kubik/FftFilter.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <vector>

#include <kfr/dft.hpp>

namespace kubik {

namespace {

namespace k = kfr;

double angularDistancePiPeriodic(double a, double b) {
    // For line orientation we need pi-periodic distance: theta and theta + pi are the same axis.
    return 0.5 * std::fabs(std::atan2(std::sin(2.0 * (a - b)), std::cos(2.0 * (a - b))));
}

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

std::vector<double> gaussianKernel1D(double sigma) {
    sigma = std::max(1e-6, sigma);
    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    const int size = 2 * radius + 1;
    std::vector<double> kernel(static_cast<std::size_t>(size), 0.0);
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double x = static_cast<double>(i) / sigma;
        const double w = std::exp(-0.5 * x * x);
        kernel[static_cast<std::size_t>(i + radius)] = w;
        sum += w;
    }
    for (double& v : kernel) {
        v /= std::max(sum, 1e-12);
    }
    return kernel;
}

std::vector<double> gaussianSmooth1D(const std::vector<double>& in, double sigma) {
    if (in.empty()) {
        return {};
    }
    const std::vector<double> kernel = gaussianKernel1D(sigma);
    const int radius = static_cast<int>(kernel.size() / 2);
    std::vector<double> out(in.size(), 0.0);
    for (int i = 0; i < static_cast<int>(in.size()); ++i) {
        double acc = 0.0;
        for (int kx = -radius; kx <= radius; ++kx) {
            const int idx = std::clamp(i + kx, 0, static_cast<int>(in.size()) - 1);
            acc += in[static_cast<std::size_t>(idx)] * kernel[static_cast<std::size_t>(kx + radius)];
        }
        out[static_cast<std::size_t>(i)] = acc;
    }
    return out;
}

std::vector<double> gaussianSmooth2D(const std::vector<double>& in, int w, int h, double sigma) {
    if (w <= 0 || h <= 0 || static_cast<int>(in.size()) < w * h) {
        return {};
    }
    const std::vector<double> kernel = gaussianKernel1D(sigma);
    const int radius = static_cast<int>(kernel.size() / 2);
    std::vector<double> tmp(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0);
    std::vector<double> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double acc = 0.0;
            for (int kx = -radius; kx <= radius; ++kx) {
                const int xx = std::clamp(x + kx, 0, w - 1);
                acc += in[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(xx)] *
                       kernel[static_cast<std::size_t>(kx + radius)];
            }
            tmp[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = acc;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double acc = 0.0;
            for (int ky = -radius; ky <= radius; ++ky) {
                const int yy = std::clamp(y + ky, 0, h - 1);
                acc += tmp[static_cast<std::size_t>(yy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] *
                       kernel[static_cast<std::size_t>(ky + radius)];
            }
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = acc;
        }
    }
    return out;
}

}  // namespace

std::vector<float> buildFootprintMask2D(const Spectrum2D& avg_spec, const FftFilter2DParams& params) {
    const int w = avg_spec.w;
    const int h = avg_spec.h;
    if (w <= 0 || h <= 0 || static_cast<int>(avg_spec.amps.size()) < w * h) {
        return {};
    }

    constexpr double sigma_bg = 15.0;
    constexpr double angle_smooth_deg = 10.0;
    constexpr double radial_sigma = 3.0;
    constexpr int n_angles = 180;

    std::vector<double> log_spec(avg_spec.amps.size(), 0.0);
    for (std::size_t i = 0; i < avg_spec.amps.size(); ++i) {
        log_spec[i] = std::log1p(std::max(0.0, avg_spec.amps[i]));
    }

    const std::vector<double> bg = gaussianSmooth2D(log_spec, w, h, sigma_bg);
    std::vector<double> res(log_spec.size(), 0.0);
    for (std::size_t i = 0; i < res.size(); ++i) {
        res[i] = log_spec[i] - bg[i];
    }

    const int cx = w / 2;
    const int cy = h / 2;
    std::vector<double> angle_profile(n_angles, 0.0);
    std::vector<int> angle_count(n_angles, 0);
    const double half_w = std::max(0.5, angle_smooth_deg) * M_PI / 180.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double th = std::fmod(std::atan2(static_cast<double>(y - cy), static_cast<double>(x - cx)) + M_PI,
                                        M_PI);
            for (int i = 0; i < n_angles; ++i) {
                const double a = (static_cast<double>(i) / static_cast<double>(n_angles - 1)) * M_PI;
                const double dt = angularDistancePiPeriodic(th, a);
                if (dt < half_w) {
                    angle_profile[static_cast<std::size_t>(i)] +=
                        res[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
                    ++angle_count[static_cast<std::size_t>(i)];
                }
            }
        }
    }
    for (int i = 0; i < n_angles; ++i) {
        if (angle_count[static_cast<std::size_t>(i)] > 0) {
            angle_profile[static_cast<std::size_t>(i)] /=
                static_cast<double>(angle_count[static_cast<std::size_t>(i)]);
        }
    }
    angle_profile = gaussianSmooth1D(angle_profile, 2.0);
    const double mean = std::accumulate(angle_profile.begin(), angle_profile.end(), 0.0) /
                        static_cast<double>(angle_profile.size());
    double var = 0.0;
    for (double v : angle_profile) {
        const double d = v - mean;
        var += d * d;
    }
    const double stddev = std::sqrt(var / std::max<std::size_t>(1, angle_profile.size()));
    const double thr = mean + params.sensitivity * stddev;

    std::vector<float> mask(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 1.0f);
    const double notch_sigma = std::max(0.5, params.notch_width_deg) * M_PI / 180.0;
    const double suppression = std::clamp(params.suppression, 0.0, 1.0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                    static_cast<std::size_t>(x);
            const double th = std::fmod(std::atan2(static_cast<double>(y - cy), static_cast<double>(x - cx)) + M_PI,
                                        M_PI);
            double gain = 1.0;
            for (int i = 0; i < n_angles; ++i) {
                if (angle_profile[static_cast<std::size_t>(i)] <= thr) {
                    continue;
                }
                const double a = (static_cast<double>(i) / static_cast<double>(n_angles - 1)) * M_PI;
                const double dt = angularDistancePiPeriodic(th, a);
                const double notch = std::exp(-(dt * dt) / (2.0 * notch_sigma * notch_sigma));
                gain *= (1.0 - suppression * notch);
            }
            mask[idx] = static_cast<float>(std::clamp(gain, 0.0, 1.0));
        }
    }

    const std::vector<double> radial = gaussianSmooth2D(res, w, h, radial_sigma);
    const double radial_mean = std::accumulate(radial.begin(), radial.end(), 0.0) / static_cast<double>(radial.size());
    double radial_var = 0.0;
    for (double v : radial) {
        const double d = v - radial_mean;
        radial_var += d * d;
    }
    const double radial_std = std::sqrt(radial_var / std::max<std::size_t>(1, radial.size()));
    const double radial_thr = radial_mean + params.sensitivity * radial_std;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                    static_cast<std::size_t>(x);
            if (radial[idx] >= radial_thr) {
                mask[idx] = 0.0f;
            }
        }
    }

    const double k_preserve = std::max(0.0, params.k_preserve);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double r = std::sqrt(static_cast<double>((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            if (r < k_preserve) {
                mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = 1.0f;
            }
        }
    }
    return mask;
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
    if (params.mask_w == w && params.mask_h == h &&
        static_cast<int>(params.mask.size()) == w * h) {
        return filterSlice2DWithMask(slice, w, h, params.mask);
    }
    const Spectrum2D spec = computeSpectrum2D(slice, w, h, d_xl, d_il);
    const std::vector<float> mask = buildFootprintMask2D(spec, params);
    return filterSlice2DWithMask(slice, w, h, mask);
}

std::vector<float> filterSlice2DWithMask(const std::vector<float>& slice, int w, int h,
                                         const std::vector<float>& mask) {
    if (w <= 0 || h <= 0 || slice.empty() || static_cast<int>(mask.size()) < w * h) {
        return slice;
    }
    std::vector<k::complex<double>> freq;
    if (!forward2D(slice, w, h, freq)) {
        return slice;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                    static_cast<std::size_t>(x);
            freq[idx] *= static_cast<double>(mask[idx]);
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
