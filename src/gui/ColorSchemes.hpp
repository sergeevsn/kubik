#ifndef KUBIK_COLORSCHEMES_HPP
#define KUBIK_COLORSCHEMES_HPP

#include <QColor>
#include <QString>
#include <QStringList>
#include <map>
#include <memory>
#include <vector>

struct ColorStop {
    float position;
    QColor color;

    ColorStop(float pos, const QColor& c) : position(pos), color(c) {}
};

class ColorScheme {
public:
    QString name;
    std::vector<ColorStop> stops;
    bool cyclic = false;
    float contrast = 1.0f;
    float brightness = 0.0f;

    explicit ColorScheme(const QString& n) : name(n) {}
    void addStop(float pos, const QColor& color);
    QColor getColor(float value) const;
};

class ColorSchemes {
public:
    static QColor getColor(float normalizedValue, const QString& schemeName);
    static QColor getColorWithParams(float normalizedValue,
                                     const QString& schemeName,
                                     float contrast = 1.0f,
                                     float brightness = 0.0f,
                                     float gamma = 1.0f);
    static std::vector<QColor> getColorPalette(const QString& schemeName, int numColors);
    static QStringList getAvailableSchemes();
    static bool hasScheme(const QString& schemeName);

    static void setCustomGamma(float gamma) { s_gamma = gamma; }
    static float getCustomGamma() { return s_gamma; }
    static void enablePerceptualCorrection(bool enable) { s_perceptualCorrection = enable; }

    static void addCustomScheme(const ColorScheme& scheme);
    static void removeCustomScheme(const QString& name);
    static ColorScheme* getScheme(const QString& name);

    static QColor getSeismicColor(float amplitude, float rms = 1.0f, bool bipolar = true);
    static std::vector<QColor> generateSeismicPalette(int numColors, float centerBias = 0.5f);

    static float normalizeValue(float v);
    static float contrastAdjust(float v, float contrast, float brightness);
    static QColor interpolateFromPalette(const std::vector<ColorStop>& stops, float value);

private:
    static QColor interpolateColor(const QColor& c1, const QColor& c2, float t, float gamma = 1.0f);
    static QColor interpolateColorLAB(const QColor& c1, const QColor& c2, float t);
    static QColor perceptualCorrection(const QColor& color);
    static void rgbToLab(float r, float g, float b, float& L, float& a, float& lab_b);
    static void labToRgb(float L, float a, float lab_b, float& r, float& g, float& b);
    static int clampInt(int v, int lo, int hi);
    static float gammaCorrect(float v, float gamma);

    static std::vector<ColorStop> getGrayStops();
    static std::vector<ColorStop> getViridisPlusStops();
    static std::vector<ColorStop> getRedBlueStops();
    static std::vector<ColorStop> getPetrelClassicStops();
    static std::vector<ColorStop> getKingdomStops();

    static float s_gamma;
    static bool s_perceptualCorrection;
    static std::map<QString, std::unique_ptr<ColorScheme>> s_customSchemes;
};

#endif
