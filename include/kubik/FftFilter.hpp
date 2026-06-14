#pragma once

#include <cstdint>
#include <vector>

namespace kubik {

enum class FftFilterType {
    Bandpass,
    Lowpass,
    Highpass,
    Notch,
};

struct FftFilterParams {
    FftFilterType type = FftFilterType::Bandpass;
    double f_low_hz = 10.0;
    double f_high_hz = 60.0;
    int order = 4;
};

struct Spectrum1D {
    std::vector<double> freqs_hz;
    std::vector<double> amps;
};

/// Средний RMS-спектр по выделенной области (1D FFT вдоль времени для каждой колонки).
Spectrum1D computeAverageSpectrum1D(const std::vector<float>& slice, int w, int h, int h0, int h1,
                                    int v0, int v1, float dt_ms);

/// Применить zero-phase Butterworth-фильтр к одной 1D трассе.
void applyFftFilter1D(std::vector<float>& trace, float dt_ms, const FftFilterParams& params);

/// Фильтрация выделенной области среза (каждая колонка — отдельная трасса).
std::vector<float> filterSliceRegion1D(const std::vector<float>& slice, int w, int h, int h0,
                                       int h1, int v0, int v1, float dt_ms,
                                       const FftFilterParams& params);

/// Фильтрация всего среза IL/XL (каждая колонка — трасса вдоль времени).
std::vector<float> filterSlice1D(const std::vector<float>& slice, int w, int h, float dt_ms,
                                 const FftFilterParams& params);

double butterworthGainSquared(double f_hz, double fs_hz, const FftFilterParams& params);

enum class FftFilter2DType {
    FootprintIlXl,
    FootprintIl,
    FootprintXl,
};

/// 2D footprint-фильтр time-среза: широкая полоса вырезания [k_pass, k_cut) вдоль оси(ей).
struct FftFilter2DParams {
    FftFilter2DType type = FftFilter2DType::FootprintIlXl;
    double k_cut_il = 0.05;
    double k_cut_xl = 0.05;
    double k_pass = 0.005;
    /// Ширина косинусного сглаживания на каждом краю зоны вырезания (в единицах k).
    /// При k_smooth >= (k_cut - k_pass) / 2 — один спад на всём интервале [k_pass, k_cut).
    double k_smooth = 0.01;
};

struct Spectrum2D {
    int w = 0;
    int h = 0;
    std::vector<double> k_xl;
    std::vector<double> k_il;
    std::vector<double> amps;
};

/// 2D амплитудный спектр (|FFT|), оси k_XL (X) и k_IL (Y), fftshift.
Spectrum2D computeSpectrum2D(const std::vector<float>& slice, int w, int h, double d_xl, double d_il);

/// Коэффициент пропускания в точке (k_il, k_xl) для footprint-маски.
double footprintGain2D(double k_il, double k_xl, const FftFilter2DParams& params);

std::vector<float> filterSlice2D(const std::vector<float>& slice, int w, int h, double d_xl, double d_il,
                                 const FftFilter2DParams& params);

std::vector<float> filterSliceRegion2D(const std::vector<float>& slice, int w, int h, int h0, int h1,
                                       int v0, int v1, double d_xl, double d_il,
                                       const FftFilter2DParams& params);

}  // namespace kubik
