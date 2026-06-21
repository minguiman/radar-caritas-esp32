#pragma once

#include "AppConfig.h"
#include "PlaneData.h"
#include "RadarModel.h"

#include <lvgl.h>

#include <cstddef>
#include <cstdint>
#include <time.h>
#include <array>
#include <vector>

struct ImageFrame565View;

class RadarRenderer
{
public:
    ~RadarRenderer();
    void attachBuffer(lv_color_t* buffer, uint16_t width, uint16_t height);
    void render(const RadarModel& model, bool wifiConnected, bool wifiConfigured, bool apiFetchPending, bool wifiNoWarningVisible, bool apiNoWarningVisible, const String& locationLabel, const String& statusText, uint8_t radarTheme, bool refreshLabels, uint8_t labelMode, bool militaryHighlightEnabled);
    int hitTestPlaneIndex(const RadarModel& model, int x, int y, uint8_t radarTheme) const;
    void renderPlanePopup(const Plane& plane, uint8_t radarTheme, bool militaryHighlightEnabled);
    void renderPlaneCornerMarker(const RadarModel& model, const String& planeId, uint8_t radarTheme, uint32_t nowMs, uint32_t untilMs);
    void renderSetupScreen(const String& apSsid, const String& apPassword, const String& apIp, const String& statusLine1, const String& statusLine2, uint8_t radarTheme);
    void renderStorageSaveScreen(bool completed, bool ok, uint32_t elapsedMs, uint32_t totalMs, uint8_t radarTheme);
    void renderClockScreen(const String& timeText, const String& secondsText, const String& dateText, const String& dayText, const String& zuluText, bool wifiConnected, bool alarmRinging, uint8_t radarTheme, float tiltX, float tiltY, float rotationRad);
    void renderWatchScreen(uint8_t hour, uint8_t minute, uint8_t second, float secondFraction, bool timeValid, bool alarmEnabled, bool alarmRinging, uint8_t alarmHour, uint8_t alarmMinute, uint8_t radarTheme);
    bool renderSkyScreen(time_t nowUtc, double latitude, double longitude, float rangeKm, bool timeValid, uint8_t radarTheme, uint8_t labelMode);
    void renderOptionsScreen(float rangeKm, uint8_t radarTheme, bool alarmEnabled, uint8_t alarmHour, uint8_t alarmMinute, bool northBeepEnabled, uint8_t brightness, uint8_t apiRefreshSeconds, uint8_t fps, uint8_t gyroFps, bool idleDimEnabled, bool militaryOnlyEnabled);
    // Vista de imagen estática: dibuja un frame RGB565 ajustado (proporción centrada
    // sobre negro). No asume que el origen sea siempre 480x480 (preparado para SD).
    void renderAlternativeClockScreen(const String& timeText, int currentMinutesOfDay, bool alarmEnabled, bool alarmRinging, uint8_t alarmHour, uint8_t alarmMinute, uint8_t weekdayIndex);
    void renderDayClockScreen(const String& dayText, const String& dateText, const String& timeText, const String& weatherTempText, const String& weatherConditionText, uint8_t minute, bool timeValid, bool weatherValid, bool alarmEnabled, uint8_t alarmHour, uint8_t alarmMinute, uint8_t radarTheme);
    void renderStartupScreen(uint32_t elapsedMs, uint32_t durationMs, uint8_t radarTheme);
    void renderImageScreen(const ImageFrame565View& frame);
    void renderPomodoroScreen(const String& title, const String& timeText, const String& stateText,
        const String& hintText, uint8_t progressPercent, bool running, bool breakPhase,
        bool settingsMode, uint8_t radarTheme);
    bool startDragonBallScan(uint32_t nowMs);
    bool dragonBallScanActive(uint32_t nowMs) const;

    // Fuerza que el siguiente render del radar normal sea de pantalla completa.
    // Necesario al volver desde otras vistas o al cerrar overlays/popup.
    void forceNextRadarFullFrame();
    void clearRadarLabels();

    bool preferPartialFlush() const;
    size_t dirtyRegionCount() const;
    bool dirtyRegionAt(size_t index, int& x, int& y, int& w, int& h) const;

private:
    struct Rect
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    struct LabelLayout
    {
        Rect rect;
        Rect collisionRect;
        int lineX = 0;
        bool textOnRight = true;
        int slotId = -1;
    };

    struct LabelCandidate
    {
        uint32_t planeKey = 0;
        int planeX = 0;
        int planeY = 0;
        float distanceKm = 0.0f;
        LabelLayout label;
        uint32_t lastSweepHitMs = 0;
    };

    struct SimpleRadarLabel
    {
        uint32_t planeKey = 0;
        int16_t planeX = 0;
        int16_t planeY = 0;
        int16_t x = 0;
        int16_t y = 0;
        uint8_t w = 0;
        uint8_t h = 0;
        int16_t markerX = 0;
        int16_t markerTop = 0;
        int16_t markerBottom = 0;
        int16_t connectorX = 0;
        int16_t connectorY = 0;
        float bearingDeg = 0.0f;
        uint8_t generation = 0;
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        uint8_t alpha = 240;
        char text[9] = {};
        // Ultimo rect realmente pintado en el framebuffer. Mientras la label no
        // cambie y el restore no pase por encima, no se repinta ni se resube.
        Rect drawnRect{};
        bool drawnValid = false;
        // Alfa cuantizada del ultimo pintado. Hereda del nivel de brillo del avion,
        // pero comprimida a 8 escalones para no repintar la label cada frame.
        uint8_t drawnAlphaLevel = 0;
    };

    // Ultimo estado pintado de cada icono de avion. Con el brillo cuantizado,
    // durante el hold del contacto el icono no cambia y se salta el repintado.
    struct PlaneIconDrawnState
    {
        uint32_t key = 0;
        int16_t x = 0;
        int16_t y = 0;
        uint8_t level = 0;
        uint8_t styleBits = 0;
        Rect rect{};
        bool seen = false;
    };

    struct FooterSlot
    {
        String text;
        Rect rect{};
        bool valid = false;
    };

    struct DragonBallMarker
    {
        String planeId;
        uint8_t number = 0;
    };

    struct DragonBallBlip
    {
        String planeId;
        uint8_t number = 0;
        int x = 0;
        int y = 0;
        int radius = 0;
        int lastHitWave = -1;
        uint32_t glowStartMs = 0;
    };

    void clear();
    void renderDragonBallRadar(const RadarModel& model, bool wifiConnected, bool wifiConfigured, bool apiFetchPending, bool wifiNoWarningVisible, bool apiNoWarningVisible);
    void rebuildDragonBallBlips(const RadarModel& model);
    void blitImage565(int dstX, int dstY, int width, int height, const uint16_t* src);
    void blitCroppedImage565(int dstX, int dstY, int width, int height, const uint16_t* src, int srcWidth, int srcHeight, int srcX, int srcY);
    void blitTransformedImage565(int dstX, int dstY, int width, int height, const uint16_t* src, int srcWidth, int srcHeight, float offsetX, float offsetY, float rotationRad);
    void blitTransformedImage565Masked(int dstX, int dstY, int width, int height, const uint16_t* src, int srcWidth, int srcHeight, float offsetX, float offsetY, float rotationRad, const uint16_t* rowStart, const uint16_t* rowEnd);
    void blitKeyedImage565(int dstX, int dstY, int width, int height, const uint16_t* src, uint16_t transparentKey);
    void blitKeyedImage565Scaled(int dstX, int dstY, int srcWidth, int srcHeight, int dstWidth, int dstHeight,
        const uint16_t* src, uint16_t transparentKey);
    void blitRgb565Image(int dstX, int dstY, int width, int height, const uint16_t* src, uint16_t transparentKey);
    void blitRgb565ImageScaled(int dstX, int dstY, int width, int height, int scale, const uint16_t* src, uint16_t transparentKey);
    void setPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue);
    void blendPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void blendPixelRGB(int x, int y, uint8_t red, uint8_t green, uint8_t blue, float alphaR, float alphaG, float alphaB);
    void fillRect(int x, int y, int width, int height, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawLine(int x0, int y0, int x1, int y1, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawCircle(int centerX, int centerY, int radius, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawCircleThick(int centerX, int centerY, int radius, int thickness, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawSquare(int centerX, int centerY, int size, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawTriangle(int centerX, int topY, int width, int height, bool pointDown, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void fillCircle(int centerX, int centerY, int radius, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    // Primitivas con antialiasing (borde suavizado por cobertura). Reutilizadas por
    // el reloj analógico y la pantalla del cielo.
    void fillCircleAA(int cx, int cy, float radius, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawThickLineAA(int x0, int y0, int x1, int y1, float thickness, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawCircleAA(int cx, int cy, float radius, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawRadarGrid(int centerX, int centerY, int radius, uint8_t red, uint8_t green, uint8_t blue);
    void drawRadarContours(int centerX, int centerY, int radius, uint8_t red, uint8_t green, uint8_t blue);
    void drawBearingScale(int centerX, int centerY, int radius, uint8_t red, uint8_t green, uint8_t blue);
    void drawSweepWedge(int centerX, int centerY, int radius, float angleDeg, uint8_t red, uint8_t green, uint8_t blue);
    void drawSweepGlow(int centerX, int centerY, int radius, float angleDeg, uint8_t red, uint8_t green, uint8_t blue);
    void drawAircraftIcon(int centerX, int centerY, float headingDeg, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawDragonBallNumber(int x, int y, uint8_t number, float alpha);
    void drawGlyph(int x, int y, char c, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawGlyphScaled(int x, int y, char c, int scale, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawGlyphSized(int x, int y, char c, int glyphWidth, int glyphHeight, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawText(int x, int y, const String& text, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawTextScaled(int x, int y, const String& text, int scale, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawTextSized(int x, int y, const String& text, int glyphHeight, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawCStringSized(int x, int y, const char* text, int glyphHeight, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawTextExpanded(int x, int y, const String& text, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawTextScaledExpanded(int x, int y, const String& text, int scale, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawTextMedium(int x, int y, const String& text, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawTextRightAligned(int rightX, int y, const String& text, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawSevenSegmentDigit(int x, int y, int width, int height, int thickness, char digit, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    void drawSevenSegmentColon(int x, int y, int height, int thickness, uint8_t red, uint8_t green, uint8_t blue, float alpha);
    int measureText(const String& text) const;
    int measureTextScaled(const String& text, int scale) const;
    int measureTextSized(const String& text, int glyphHeight) const;
    bool intersects(const Rect& left, const Rect& right) const;
    bool projectPlaneToScreen(const RadarModel& model, const Plane& plane, int& x, int& y) const;
    void beginFullFrame();
    void beginPartialRadarFrame();
    void setFullScreenDirty();
    void clearCurrentDirtyRegions();
    void markDirtyRect(const Rect& rect);
    void markDirtyRectExpanded(int x, int y, int w, int h, int padding = 2);
    void copyRect(const lv_color_t* src, lv_color_t* dst, const Rect& rect);
    void restorePreviousDirtyRegions();
    void finalizePartialRadarFrame();
    LabelLayout computeLabelLayout(int centerX, int centerY, int planeX, int planeY) const;
    std::vector<LabelLayout> computeLabelLayoutOptions(int centerX, int centerY, int planeX, int planeY) const;
    void computeLabelLayoutOptionsInto(int centerX, int centerY, int planeX, int planeY, std::vector<LabelLayout>& layouts) const;
    void drawLabel(const VisiblePlane& visible, int planeX, int planeY, const LabelLayout& layout, bool showType, uint8_t red, uint8_t green, uint8_t blue);

    lv_color_t* m_buffer = nullptr;
    lv_color_t* m_radarBackground = nullptr;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    size_t m_frameBytes = 0;
    int m_dragonBallScreenRadius = 0;
    uint16_t m_cachedRadarWidth = 0;
    uint16_t m_cachedRadarHeight = 0;
    float m_cachedRadarRangeKm = -1.0f;
    uint8_t m_cachedRadarTheme = 255;
    bool m_forceNextRadarFullFrame = true;
    bool m_preferPartialFlush = false;
    std::vector<Rect> m_currentDirtyRegions;
    std::vector<Rect> m_previousDirtyRegions;
    std::vector<Rect> m_uploadDirtyRegions;
    static constexpr size_t kMaxSimpleRadarLabels = 20;
    std::array<SimpleRadarLabel, kMaxSimpleRadarLabels> m_simpleRadarLabels;
    uint8_t m_simpleRadarLabelCount = 0;
    std::array<SimpleRadarLabel, kMaxSimpleRadarLabels> m_pendingSimpleRadarLabels;
    uint8_t m_pendingSimpleRadarLabelCount = 0;
    uint8_t m_simpleRadarLabelGeneration = 0;
    bool m_pendingSimpleRadarLabelsActive = false;
    bool m_simpleRadarLabelSnapshotValid = false;
    uint32_t m_simpleRadarLabelSnapshotSignature = 0;
    float m_simpleRadarLabelSnapshotRangeKm = -1.0f;
    uint8_t m_simpleRadarLabelSnapshotTheme = 255;
    uint8_t m_simpleRadarLabelSnapshotMode = 255;
    bool m_simpleRadarLabelSnapshotMilitary = false;
    uint32_t m_radarFrameCounter = 0;
    std::array<PlaneIconDrawnState, static_cast<size_t>(AppConfig::kMaxVisiblePlanes)> m_planeIconStates{};
    uint8_t m_planeIconStateCount = 0;
    FooterSlot m_footerLocation;
    FooterSlot m_footerStatus;
    std::vector<LabelCandidate> m_frozenSelectedLabels; // legacy, unused by simple labels
    std::vector<size_t> m_labelOrderScratch; // legacy scratch kept for ABI/source stability
    std::vector<LabelLayout> m_labelLayoutScratch; // legacy scratch kept for ABI/source stability
    std::vector<DragonBallMarker> m_dragonBallMarkers;
    std::vector<DragonBallBlip> m_dragonBallBlips;
    bool m_hasDragonBallSweepAngle = false;
    float m_lastDragonBallSweepAngle = 0.0f;
    uint32_t m_dragonBallPulseStartMs = 0;
    bool m_dragonBallScanActive = false;
    uint32_t m_dragonBallScanStartMs = 0;

    // Sky view caches
    struct SkyProj {
        int16_t x = 0, y = 0;
        float azDeg = 0.0f;
        bool visible = false;
    };

    struct SkyDustDot {
        int16_t x = 0;
        int16_t y = 0;
        uint8_t alpha = 0;
    };
    lv_color_t* m_skyBackground = nullptr;
    uint16_t m_cachedSkyBgWidth = 0;
    uint16_t m_cachedSkyBgHeight = 0;
    uint8_t m_cachedSkyBgTheme = 255;
    uint8_t m_cachedSkyBgMode = 255;
    std::vector<SkyProj> m_cachedSkyStars;
    std::vector<SkyProj> m_cachedSkySegFrom;
    std::vector<SkyProj> m_cachedSkySegTo;
    std::vector<SkyProj> m_cachedSkyLabels;
    std::vector<SkyProj> m_cachedSkyMw;
    std::vector<SkyDustDot> m_cachedMilkyWayDust;
    std::vector<SkyProj> m_cachedFaintStars;
    SkyProj m_cachedSun, m_cachedMoon;
    SkyProj m_cachedPlanets[5];
    time_t m_lastSkyProjTime = 0;
    double m_lastSkyProjLat = 0;
    double m_lastSkyProjLon = 0;
    uint16_t m_lastSkyProjWidth = 0;
    uint16_t m_lastSkyProjHeight = 0;
    float m_cachedSunRa = 0.0f;
    float m_cachedMoonRa = 0.0f;


    lv_color_t* m_optionsFrameCache = nullptr;
    size_t m_optionsFrameCacheBytes = 0;
    uint16_t m_cachedOptionsFrameWidth = 0;
    uint16_t m_cachedOptionsFrameHeight = 0;
    String m_cachedOptionsFrameKey;
    bool m_cachedOptionsFrameValid = false;
    bool m_cachedSkyFrameValid = false;
    time_t m_lastSkyRenderTimeBucket = 0;
    double m_lastSkyRenderLat = 0;
    double m_lastSkyRenderLon = 0;
    uint16_t m_lastSkyRenderWidth = 0;
    uint16_t m_lastSkyRenderHeight = 0;
    uint8_t m_lastSkyRenderTheme = 255;
    uint8_t m_lastSkyRenderLabelMode = 255;
    bool m_lastSkyRenderTimeValid = false;

    lv_color_t* m_watchBackground = nullptr;
    uint16_t m_cachedWatchBgWidth = 0;
    uint16_t m_cachedWatchBgHeight = 0;
    lv_color_t* m_alternativeClockBackground = nullptr;
    uint16_t m_cachedAlternativeClockBgWidth = 0;
    uint16_t m_cachedAlternativeClockBgHeight = 0;

public:
    lv_color_t* buffer() const { return m_buffer; }
    uint16_t width() const { return m_width; }
    uint16_t height() const { return m_height; }
};
