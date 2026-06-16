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

struct FftFilter2DParams {
    /// Ширина азимутального notch (сигма, в градусах).
    double notch_width_deg = 6.0;
    /// Чувствительность детекции в std: mean + sensitivity * std.
    double sensitivity = 2.0;
    /// Сила подавления для обнаруженных направлений: 0..1.
    double suppression = 1.0;
    /// Радиус (в пикселях k-space) вокруг k=0, который всегда сохраняется.
    double k_preserve = 5.0;
    /// Число time-слайсов для осреднения спектра (окно вокруг текущего слайса).
    int avg_slice_count = 50;

    /// Предрассчитанная маска для применения к срезам и при сохранении куба.
    int mask_w = 0;
    int mask_h = 0;
    std::vector<float> mask;
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

/// Построить footprint-маску по среднему 2D-спектру.
/// Вход: avg_spec.amps — амплитуды |FFT| (fftshift) усреднённого time-окна.
/// Выход: маска [0..1], размер w*h.
std::vector<float> buildFootprintMask2D(const Spectrum2D& avg_spec, const FftFilter2DParams& params);

/// Применить готовую маску к 2D time-срезу.
std::vector<float> filterSlice2DWithMask(const std::vector<float>& slice, int w, int h,
                                         const std::vector<float>& mask);

std::vector<float> filterSlice2D(const std::vector<float>& slice, int w, int h, double d_xl, double d_il,
                                 const FftFilter2DParams& params);

std::vector<float> filterSliceRegion2D(const std::vector<float>& slice, int w, int h, int h0, int h1,
                                       int v0, int v1, double d_xl, double d_il,
                                       const FftFilter2DParams& params);

}  // namespace kubik
