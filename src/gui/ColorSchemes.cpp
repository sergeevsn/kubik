#include "ColorSchemes.hpp"

#include <algorithm>
#include <cmath>

float ColorSchemes::s_gamma = 1.0f;
bool ColorSchemes::s_perceptualCorrection = false;
std::map<QString, std::unique_ptr<ColorScheme>> ColorSchemes::s_customSchemes;

namespace {
const float XYZ_WHITE_X = 95.047f;
const float XYZ_WHITE_Y = 100.000f;
const float XYZ_WHITE_Z = 108.883f;

inline float sRGBtoLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float linearTosRGB(float c) {
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline float labF(float t) {
    const float delta = 6.0f / 29.0f;
    return t > delta * delta * delta ? std::pow(t, 1.0f / 3.0f)
                                     : t / (3.0f * delta * delta) + 4.0f / 29.0f;
}

inline float labFInv(float t) {
    const float delta = 6.0f / 29.0f;
    return t > delta ? t * t * t : 3.0f * delta * delta * (t - 4.0f / 29.0f);
}
}  // namespace

void ColorScheme::addStop(float pos, const QColor& color) {
    stops.emplace_back(pos, color);
    std::sort(stops.begin(), stops.end(),
              [](const ColorStop& a, const ColorStop& b) { return a.position < b.position; });
}

QColor ColorScheme::getColor(float value) const {
    if (stops.empty()) return QColor(0, 0, 0);
    if (stops.size() == 1) return stops[0].color;
    value = ColorSchemes::normalizeValue(value);
    value = ColorSchemes::contrastAdjust(value, contrast, brightness);
    return ColorSchemes::interpolateFromPalette(stops, value);
}

void ColorSchemes::rgbToLab(float r, float g, float b, float& L, float& a, float& lab_b) {
    r = sRGBtoLinear(r);
    g = sRGBtoLinear(g);
    b = sRGBtoLinear(b);
    float x = 0.4124564f * r + 0.3575761f * g + 0.1804375f * b;
    float y = 0.2126729f * r + 0.7151522f * g + 0.0721750f * b;
    float z = 0.0193339f * r + 0.1191920f * g + 0.9503041f * b;
    x = labF(x / XYZ_WHITE_X);
    y = labF(y / XYZ_WHITE_Y);
    z = labF(z / XYZ_WHITE_Z);
    L = 116.0f * y - 16.0f;
    a = 500.0f * (x - y);
    lab_b = 200.0f * (y - z);
}

void ColorSchemes::labToRgb(float L, float a, float lab_b, float& r, float& g, float& b) {
    float y = (L + 16.0f) / 116.0f;
    float x = a / 500.0f + y;
    float z = y - lab_b / 200.0f;
    x = XYZ_WHITE_X * labFInv(x);
    y = XYZ_WHITE_Y * labFInv(y);
    z = XYZ_WHITE_Z * labFInv(z);
    r = 3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
    g = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
    b = 0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
    r = linearTosRGB(r);
    g = linearTosRGB(g);
    b = linearTosRGB(b);
    r = std::max(0.0f, std::min(1.0f, r));
    g = std::max(0.0f, std::min(1.0f, g));
    b = std::max(0.0f, std::min(1.0f, b));
}

QColor ColorSchemes::interpolateColorLAB(const QColor& c1, const QColor& c2, float t) {
    float L1, a1, b1, L2, a2, b2;
    rgbToLab(c1.redF(), c1.greenF(), c1.blueF(), L1, a1, b1);
    rgbToLab(c2.redF(), c2.greenF(), c2.blueF(), L2, a2, b2);
    float L = L1 + t * (L2 - L1);
    float a = a1 + t * (a2 - a1);
    float lab_b = b1 + t * (b2 - b1);
    float r, g, b;
    labToRgb(L, a, lab_b, r, g, b);
    return QColor::fromRgbF(r, g, b);
}

QColor ColorSchemes::interpolateColor(const QColor& c1, const QColor& c2, float t, float gamma) {
    t = normalizeValue(t);
    if (s_perceptualCorrection) {
        return interpolateColorLAB(c1, c2, t);
    }
    const float gammaT = gammaCorrect(t, gamma);
    const int r = static_cast<int>(c1.red() * (1.0f - gammaT) + c2.red() * gammaT + 0.5f);
    const int g = static_cast<int>(c1.green() * (1.0f - gammaT) + c2.green() * gammaT + 0.5f);
    const int b = static_cast<int>(c1.blue() * (1.0f - gammaT) + c2.blue() * gammaT + 0.5f);
    return QColor(clampInt(r, 0, 255), clampInt(g, 0, 255), clampInt(b, 0, 255));
}

QColor ColorSchemes::interpolateFromPalette(const std::vector<ColorStop>& stops, float value) {
    if (stops.empty()) return QColor(0, 0, 0);
    if (stops.size() == 1) return stops[0].color;
    value = normalizeValue(value);
    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        if (value >= stops[i].position && value <= stops[i + 1].position) {
            const float range = stops[i + 1].position - stops[i].position;
            if (range < 1e-6f) return stops[i].color;
            const float t = (value - stops[i].position) / range;
            return interpolateColor(stops[i].color, stops[i + 1].color, t, s_gamma);
        }
    }
    if (value < stops[0].position) return stops[0].color;
    return stops.back().color;
}

float ColorSchemes::contrastAdjust(float v, float contrast, float brightness) {
    v = normalizeValue(v);
    v = 0.5f + contrast * (v - 0.5f);
    v += brightness;
    return normalizeValue(v);
}

float ColorSchemes::normalizeValue(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

int ColorSchemes::clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(v, hi));
}

float ColorSchemes::gammaCorrect(float v, float gamma) {
    return std::pow(v, 1.0f / gamma);
}

std::vector<ColorStop> ColorSchemes::getGrayStops() {
    return {{0.00f, QColor(255, 255, 255)}, {1.00f, QColor(0, 0, 0)}};
}

std::vector<ColorStop> ColorSchemes::getViridisPlusStops() {
    return {{0.00f, QColor(68, 1, 84)},   {0.10f, QColor(72, 35, 116)},
            {0.20f, QColor(64, 67, 135)},  {0.30f, QColor(52, 94, 141)},
            {0.40f, QColor(41, 120, 142)}, {0.50f, QColor(32, 144, 140)},
            {0.60f, QColor(34, 167, 132)}, {0.70f, QColor(37, 188, 121)},
            {0.80f, QColor(65, 204, 103)},  {0.90f, QColor(119, 216, 67)},
            {1.00f, QColor(253, 231, 37)}};
}

std::vector<ColorStop> ColorSchemes::getRedBlueStops() {
    return {{0.00f, QColor(0, 0, 255)}, {0.50f, QColor(255, 255, 255)}, {1.00f, QColor(255, 0, 0)}};
}

std::vector<ColorStop> ColorSchemes::getPetrelClassicStops() {
    return {{0.00f, QColor(0, 0, 90)},    {0.15f, QColor(0, 60, 160)},
            {0.25f, QColor(0, 120, 220)},  {0.35f, QColor(80, 180, 255)},
            {0.45f, QColor(200, 230, 255)}, {0.50f, QColor(255, 255, 255)},
            {0.55f, QColor(255, 230, 200)}, {0.65f, QColor(255, 180, 80)},
            {0.75f, QColor(220, 120, 0)},   {0.85f, QColor(160, 60, 0)},
            {1.00f, QColor(90, 0, 0)}};
}

std::vector<ColorStop> ColorSchemes::getKingdomStops() {
    return {{0.00f, QColor(20, 20, 120)},  {0.12f, QColor(0, 80, 180)},
            {0.25f, QColor(0, 140, 240)},   {0.37f, QColor(100, 200, 255)},
            {0.45f, QColor(180, 240, 255)}, {0.50f, QColor(248, 248, 248)},
            {0.55f, QColor(255, 240, 180)}, {0.63f, QColor(255, 200, 100)},
            {0.75f, QColor(240, 140, 0)},   {0.88f, QColor(180, 80, 0)},
            {1.00f, QColor(120, 20, 20)}};
}

QColor ColorSchemes::getColor(float normalizedValue, const QString& schemeName) {
    normalizedValue = normalizeValue(normalizedValue);
    if (schemeName == "gray") return interpolateFromPalette(getGrayStops(), normalizedValue);
    if (schemeName == "viridis") return interpolateFromPalette(getViridisPlusStops(), normalizedValue);
    if (schemeName == "red_blue") return interpolateFromPalette(getRedBlueStops(), normalizedValue);
    if (schemeName == "petrel_classic") return interpolateFromPalette(getPetrelClassicStops(), normalizedValue);
    if (schemeName == "kingdom") return interpolateFromPalette(getKingdomStops(), normalizedValue);
    const auto it = s_customSchemes.find(schemeName);
    if (it != s_customSchemes.end()) return it->second->getColor(normalizedValue);
    return interpolateFromPalette(getGrayStops(), normalizedValue);
}

QColor ColorSchemes::getColorWithParams(float normalizedValue,
                                        const QString& schemeName,
                                        float contrast,
                                        float brightness,
                                        float gamma) {
    normalizedValue = contrastAdjust(normalizedValue, contrast, brightness);
    const float oldGamma = s_gamma;
    s_gamma = gamma;
    const QColor result = getColor(normalizedValue, schemeName);
    s_gamma = oldGamma;
    return result;
}

std::vector<QColor> ColorSchemes::getColorPalette(const QString& schemeName, int numColors) {
    std::vector<QColor> palette;
    palette.reserve(static_cast<std::size_t>(numColors));
    for (int i = 0; i < numColors; ++i) {
        const float value = static_cast<float>(i) / static_cast<float>(numColors - 1);
        palette.push_back(getColor(value, schemeName));
    }
    return palette;
}

QStringList ColorSchemes::getAvailableSchemes() {
    QStringList schemes = {"gray", "viridis", "red_blue", "petrel_classic", "kingdom"};
    for (const auto& kv : s_customSchemes) {
        schemes.append(kv.first);
    }
    return schemes;
}

bool ColorSchemes::hasScheme(const QString& schemeName) {
    return getAvailableSchemes().contains(schemeName);
}

void ColorSchemes::addCustomScheme(const ColorScheme& scheme) {
    s_customSchemes[scheme.name] = std::make_unique<ColorScheme>(scheme);
}

void ColorSchemes::removeCustomScheme(const QString& name) {
    s_customSchemes.erase(name);
}

ColorScheme* ColorSchemes::getScheme(const QString& name) {
    const auto it = s_customSchemes.find(name);
    return it != s_customSchemes.end() ? it->second.get() : nullptr;
}

QColor ColorSchemes::getSeismicColor(float amplitude, float rms, bool bipolar) {
    float normalized;
    if (bipolar) {
        normalized = (amplitude / (rms * 3.0f) + 1.0f) / 2.0f;
    } else {
        normalized = std::abs(amplitude) / (rms * 3.0f);
    }
    return interpolateFromPalette(getPetrelClassicStops(), normalized);
}

std::vector<QColor> ColorSchemes::generateSeismicPalette(int numColors, float centerBias) {
    std::vector<QColor> palette;
    palette.reserve(static_cast<std::size_t>(numColors));
    for (int i = 0; i < numColors; ++i) {
        float value = static_cast<float>(i) / static_cast<float>(numColors - 1);
        if (centerBias != 0.5f) value = std::pow(value, centerBias);
        palette.push_back(getColor(value, "petrel_classic"));
    }
    return palette;
}

QColor ColorSchemes::perceptualCorrection(const QColor& color) {
    Q_UNUSED(color);
    return color;
}
