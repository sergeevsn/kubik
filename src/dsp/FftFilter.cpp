#include "kubik/FftFilter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <kfr/dft.hpp>

namespace kubik {

namespace {

namespace k = kfr;

double butterworthBandpassSquared(double f, double fs, double f_low, double f_high, int order) {
    if (f <= 0.0) {
        return 0.0;
    }
    const double w = 2.0 * M_PI * f / fs;
    const double w_low = 2.0 * M_PI * f_low / fs;
    const double w_high = 2.0 * M_PI * f_high / fs;
    const double w0 = std::sqrt(w_low * w_high);
    const double bw = w_high - w_low;
    if (bw <= 0.0) {
        return 0.0;
    }
    const double temp = (w * w - w0 * w0) / (w * bw);
    return 1.0 / (1.0 + std::pow(temp * temp, static_cast<double>(order)));
}

double butterworthLowpassSquared(double f, double fs, double fc, int order) {
    if (f <= 0.0) {
        return 1.0;
    }
    if (fc <= 0.0) {
        return 0.0;
    }
    return 1.0 / (1.0 + std::pow(f / fc, 2.0 * static_cast<double>(order)));
}

double butterworthHighpassSquared(double f, double fs, double fc, int order) {
    if (f <= 0.0) {
        return 0.0;
    }
    if (fc <= 0.0) {
        return 1.0;
    }
    return 1.0 / (1.0 + std::pow(fc / f, 2.0 * static_cast<double>(order)));
}

std::size_t evenFftSize(std::size_t n) {
    if (n < 2) {
        return 0;
    }
    return (n % 2 == 0) ? n : (n + 1);
}

struct FftPlanCache {
    std::size_t n = 0;
    std::optional<k::dft_plan_real<double>> plan;
    k::univector<double> in;
    k::univector<k::complex<double>> freq;
    k::univector<k::u8> temp;

    void ensure(std::size_t fft_n) {
        if (fft_n < 2 || fft_n == n) {
            return;
        }
        n = fft_n;
        plan.emplace(n);
        in.resize(n);
        freq.resize(plan->complex_size());
        temp.resize(std::max(plan->temp_size, std::size_t(1)));
    }
};

FftPlanCache& fftCache() {
    thread_local FftPlanCache cache;
    return cache;
}

void forwardSpectrum(const double* samples, std::size_t count, double fs,
                     std::vector<double>& out_freqs, std::vector<double>& out_amps) {
    const std::size_t fft_n = evenFftSize(count);
    if (fft_n < 2 || fs <= 0.0) {
        out_freqs.clear();
        out_amps.clear();
        return;
    }

    FftPlanCache& cache = fftCache();
    cache.ensure(fft_n);
    for (std::size_t i = 0; i < count; ++i) {
        cache.in[i] = samples[i];
    }
    for (std::size_t i = count; i < fft_n; ++i) {
        cache.in[i] = 0.0;
    }

    cache.plan->execute(cache.freq, cache.in, cache.temp.data(), k::cdirect_t{});

    const int n = static_cast<int>(fft_n);
    const double df = fs / static_cast<double>(n);
    const std::size_t n_half = cache.plan->complex_size();
    out_freqs.resize(n_half);
    out_amps.resize(n_half);
    for (std::size_t i = 0; i < n_half; ++i) {
        out_freqs[i] = static_cast<double>(i) * df;
        out_amps[i] = std::abs(cache.freq[i]);
    }
}

void filterTraceInPlace(std::vector<float>& trace, float dt_ms, const FftFilterParams& params) {
    const std::size_t size_n = trace.size();
    if (size_n == 0 || dt_ms <= 0.f) {
        return;
    }
    const std::size_t fft_n = evenFftSize(size_n);
    if (fft_n < 2) {
        return;
    }

    const double fs = 1000.0 / static_cast<double>(dt_ms);
    FftPlanCache& cache = fftCache();
    cache.ensure(fft_n);

    for (std::size_t i = 0; i < size_n; ++i) {
        cache.in[i] = static_cast<double>(trace[i]);
    }
    if (fft_n > size_n) {
        cache.in[size_n] = 0.0;
    }

    cache.plan->execute(cache.freq, cache.in, cache.temp.data(), k::cdirect_t{});

    const int n = static_cast<int>(fft_n);
    const double df = fs / static_cast<double>(n);
    const std::size_t n_half = cache.plan->complex_size();
    for (std::size_t i = 0; i < n_half; ++i) {
        const double freq = static_cast<double>(i) * df;
        const double gain2 = butterworthGainSquared(freq, fs, params);
        cache.freq[i] *= gain2;
    }

    cache.plan->execute(cache.in, cache.freq, cache.temp.data(), k::cinvert_t{});

    const double norm = 1.0 / static_cast<double>(n);
    for (std::size_t i = 0; i < size_n; ++i) {
        trace[i] = static_cast<float>(cache.in[i] * norm);
    }
}

}  // namespace

double butterworthGainSquared(double f_hz, double fs_hz, const FftFilterParams& params) {
    switch (params.type) {
    case FftFilterType::Bandpass:
        return butterworthBandpassSquared(f_hz, fs_hz, params.f_low_hz, params.f_high_hz, params.order);
    case FftFilterType::Lowpass:
        return butterworthLowpassSquared(f_hz, fs_hz, params.f_high_hz, params.order);
    case FftFilterType::Highpass:
        return butterworthHighpassSquared(f_hz, fs_hz, params.f_low_hz, params.order);
    case FftFilterType::Notch:
        return 1.0 - butterworthBandpassSquared(f_hz, fs_hz, params.f_low_hz, params.f_high_hz, params.order);
    }
    return 1.0;
}

Spectrum1D computeAverageSpectrum1D(const std::vector<float>& slice, int w, int h, int h0, int h1,
                                    int v0, int v1, float dt_ms) {
    Spectrum1D result;
    if (slice.empty() || w <= 0 || h <= 0 || dt_ms <= 0.f) {
        return result;
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

    const int n_samples = v1 - v0 + 1;
    if (n_samples < 2) {
        return result;
    }

    const double fs = 1000.0 / static_cast<double>(dt_ms);
    const std::size_t fft_n = evenFftSize(static_cast<std::size_t>(n_samples));
    if (fft_n < 2) {
        return result;
    }

    FftPlanCache& cache = fftCache();
    cache.ensure(fft_n);
    const std::size_t n_half = cache.plan->complex_size();
    const double df = fs / static_cast<double>(fft_n);

    std::vector<double> sumsq(n_half, 0.0);
    int used = 0;
    std::vector<double> col(static_cast<std::size_t>(n_samples));

    for (int h = h0; h <= h1; ++h) {
        for (int v = 0; v < n_samples; ++v) {
            col[static_cast<std::size_t>(v)] =
                static_cast<double>(slice[static_cast<std::size_t>(v0 + v) * static_cast<std::size_t>(w) +
                                          static_cast<std::size_t>(h)]);
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(n_samples); ++i) {
            cache.in[i] = col[i];
        }
        for (std::size_t i = static_cast<std::size_t>(n_samples); i < fft_n; ++i) {
            cache.in[i] = 0.0;
        }
        cache.plan->execute(cache.freq, cache.in, cache.temp.data(), k::cdirect_t{});
        for (std::size_t i = 0; i < n_half; ++i) {
            const double a = std::abs(cache.freq[i]);
            sumsq[i] += a * a;
        }
        ++used;
    }

    if (used == 0) {
        return result;
    }

    result.freqs_hz.resize(n_half);
    result.amps.resize(n_half);
    for (std::size_t i = 0; i < n_half; ++i) {
        result.freqs_hz[i] = static_cast<double>(i) * df;
        const double mean_sq = sumsq[i] / static_cast<double>(used);
        result.amps[i] = std::sqrt(std::max(0.0, mean_sq));
    }
    return result;
}

void applyFftFilter1D(std::vector<float>& trace, float dt_ms, const FftFilterParams& params) {
    filterTraceInPlace(trace, dt_ms, params);
}

std::vector<float> filterSliceRegion1D(const std::vector<float>& slice, int w, int h, int h0,
                                       int h1, int v0, int v1, float dt_ms,
                                       const FftFilterParams& params) {
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

    std::vector<float> col(static_cast<std::size_t>(v1 - v0 + 1));
    for (int col_h = h0; col_h <= h1; ++col_h) {
        for (int v = v0; v <= v1; ++v) {
            col[static_cast<std::size_t>(v - v0)] =
                out[static_cast<std::size_t>(v) * static_cast<std::size_t>(w) + static_cast<std::size_t>(col_h)];
        }
        applyFftFilter1D(col, dt_ms, params);
        for (int v = v0; v <= v1; ++v) {
            out[static_cast<std::size_t>(v) * static_cast<std::size_t>(w) + static_cast<std::size_t>(col_h)] =
                col[static_cast<std::size_t>(v - v0)];
        }
    }
    return out;
}

std::vector<float> filterSlice1D(const std::vector<float>& slice, int w, int h, float dt_ms,
                                 const FftFilterParams& params) {
    if (w <= 0 || h <= 0) {
        return slice;
    }
    return filterSliceRegion1D(slice, w, h, 0, w - 1, 0, h - 1, dt_ms, params);
}

}  // namespace kubik
