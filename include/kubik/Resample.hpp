#pragma once

#include <cstdint>
#include <vector>

namespace kubik {

struct ResampleParams {
    float dt_out_ms = 0.f;
    float d_inline_out = 0.f;
    float d_crossline_out = 0.f;
};

struct NativeGridSteps {
    float dt_ms = 0.f;
    float d_inline = 0.f;
    float d_crossline = 0.f;
};

/// Средний шаг между соседними метками; 0 если шаг неравномерный или дробный.
float uniformLabelStep(const std::vector<int32_t>& labels);

std::vector<int32_t> buildLabelAxis(int32_t min_label, int32_t max_label, float step);
std::vector<int32_t> buildTimeMsAxis(int t_min_idx, int t_max_idx, float dt_in_ms, float dt_out_ms);

bool needsTimeResample(float dt_in_ms, float dt_out_ms);
bool needsSpatialResample(float d_native, float d_out);

/// Потрассовый ресемплинг по времени с антиалиасингом (Lanczos).
std::vector<float> resampleTrace1D(const float* samples, int n_in, float dt_in_ms, int t_min_idx,
                                   int t_max_idx, float dt_out_ms, int& n_out);

/// 2D ресемплинг среза (row-major: h строк × w столбцов).
std::vector<float> resampleSpatial2D(const float* data, int w_in, int h_in,
                                     const std::vector<int32_t>& x_labels_in,
                                     const std::vector<int32_t>& y_labels_in, int32_t x_min_label,
                                     int32_t x_max_label, int32_t y_min_label, int32_t y_max_label,
                                     float dx_out, float dy_out, std::vector<int32_t>& x_labels_out,
                                     std::vector<int32_t>& y_labels_out, int& w_out, int& h_out);

}  // namespace kubik
