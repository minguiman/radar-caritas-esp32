#include "RadarApp.h"
#include "DebugLog.h"

#include "ImageSource.h"
#include <Arduino.h>

#include <WiFi.h>
#include <esp_sntp.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <sys/time.h>

namespace
{
RadarApp* g_activeRadarApp = nullptr;
volatile bool g_touchInterruptPending = false;
constexpr int kTouchInterruptPin = 16;
constexpr int kTouchInterruptActiveLevel = LOW;
constexpr uint32_t kClockIdleFrameMs = 1000;
constexpr uint32_t kClockFastImuUpdateMs = 50;
constexpr uint32_t kClockIdleImuUpdateMs = 120;
constexpr uint32_t kClockIdleImuDelayMs = 4000;
constexpr uint32_t kSkyFrameMs = 60UL * 1000UL;
constexpr uint32_t kDayClockUnknownTimeFrameMs = 10UL * 1000UL;
constexpr uint32_t kDayClockWeatherRefreshMs = 2UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kDayClockWeatherRetryMs = 60UL * 1000UL;
constexpr uint32_t kWatchFrameMs = 100; // reloj anal�gico a ~10 FPS para un arco de segundos fluido
constexpr uint32_t kPomodoroFrameMs = 500;
constexpr uint32_t kPomodoroUpdateIntervalMs = 1000;
constexpr uint32_t kReminderVisibleMs = 18UL * 1000UL;
constexpr uint32_t kWifiNoWarningDelayMs = 4000;
constexpr uint32_t kRadarWifiWarningGraceMs = 10UL * 1000UL;
constexpr uint8_t kRadarLabelRebuildNorthPasses = 2;
constexpr uint32_t kDeselectedPlaneMarkerMs = 6000;
constexpr uint32_t kOptionsTouchPollIntervalMs = 12;
constexpr uint32_t kRuntimeConfigSaveDebounceMs = 1000;
constexpr uint32_t kRuntimeConfigSaveRetryMs = 15UL * 1000UL;
constexpr uint32_t kRuntimeConfigSaveModalMinMs = 1000;
constexpr uint32_t kRuntimeConfigSaveModalPreSaveMs = 250;
constexpr uint32_t kRuntimeConfigSaveModalFrameMs = 250;
constexpr uint32_t kRuntimeConfigSaveTouchSettleMs = 350;
constexpr uint32_t kIdleDimStartMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kIdleDimStepMs = 60UL * 1000UL;
constexpr uint8_t kIdleDimStepPercent = 10;
constexpr uint8_t kIdleDimMinBrightness = 10;
constexpr uint32_t kWiFiProfileConnectTimeoutMs = 8000;
constexpr uint32_t kWiFiProfilePollMs = 250;
constexpr uint32_t kWiFiScanMaxMs = 5000;
constexpr int kWifiNotVisibleRssi = -1000;
constexpr uint16_t kBuzzerStartCue[] = {70, 45, 100, 45, 150};
constexpr uint16_t kBuzzerPauseCue[] = {80, 45, 80, 45, 230};
constexpr uint16_t kBuzzerResetCue[] = {230, 90, 120, 70, 320};
constexpr uint16_t kBuzzerWaterCue[] = {90, 100, 90, 420, 90, 100, 90};
constexpr uint16_t kBuzzerEyesCue[] = {60, 80, 60, 80, 60};
constexpr uint16_t kBuzzerFocusCompleteCue[] = {70, 40, 70, 40, 120, 50, 180};
constexpr uint16_t kBuzzerBreakCompleteCue[] = {150, 70, 70, 70, 150};

bool ssidEqualsIgnoreCase(const String& a, const String& b)
{
    if (a.length() != b.length()) {
        return false;
    }
    for (size_t i = 0; i < a.length(); ++i) {
        if (tolower(static_cast<unsigned char>(a.charAt(i)))
            != tolower(static_cast<unsigned char>(b.charAt(i)))) {
            return false;
        }
    }
    return true;
}

int scanRssiForSsid(const String& ssid, int scanCount)
{
    if (ssid.isEmpty() || scanCount <= 0) {
        return kWifiNotVisibleRssi;
    }

    int bestRssi = kWifiNotVisibleRssi;
    for (int i = 0; i < scanCount; ++i) {
        if (ssidEqualsIgnoreCase(WiFi.SSID(i), ssid)) {
            bestRssi = max(bestRssi, static_cast<int>(WiFi.RSSI(i)));
        }
    }
    return bestRssi;
}

void sortWifiProfilesByScan(int* profileOrder, int* profileRssi, int profileCount)
{
    for (int i = 0; i < profileCount - 1; ++i) {
        for (int j = i + 1; j < profileCount; ++j) {
            if (profileRssi[j] > profileRssi[i]) {
                const int tmpIdx = profileOrder[i];
                profileOrder[i] = profileOrder[j];
                profileOrder[j] = tmpIdx;
                const int tmpRssi = profileRssi[i];
                profileRssi[i] = profileRssi[j];
                profileRssi[j] = tmpRssi;
            }
        }
    }
}

void promoteWifiProfileToFront(int* profileOrder, int* profileRssi, int profileCount, int profileIndex)
{
  if (profileCount <= 1 || profileIndex < 0 || profileIndex >= AppConfig::kMaxWifiProfiles)
  {
    return;
  }

  int slot = -1;
  for (int i = 0; i < profileCount; ++i)
  {
    if (profileOrder[i] == profileIndex)
    {
      slot = i;
      break;
    }
  }

  if (slot <= 0)
  {
    return;
  }

  const bool preferredVisible = profileRssi[slot] != kWifiNotVisibleRssi;
  const bool firstVisible = profileRssi[0] != kWifiNotVisibleRssi;
  if (!preferredVisible && firstVisible)
  {
    return;
  }

  const int preferredOrder = profileOrder[slot];
  const int preferredRssi = profileRssi[slot];
  for (int i = slot; i > 0; --i)
  {
    profileOrder[i] = profileOrder[i - 1];
    profileRssi[i] = profileRssi[i - 1];
  }
  profileOrder[0] = preferredOrder;
  profileRssi[0] = preferredRssi;
}
constexpr uint32_t kNtpResyncMs = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kNtpRetryMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kNtpWaitMs = 6000;
constexpr time_t kValidTimeEpoch = 1704067200; // 2024-01-01 00:00:00 UTC
constexpr float kClockTiltRenderThreshold = 0.006f;
constexpr float kClockRotationRenderThresholdRad = 0.006f;


struct OptionsTouchBox
{
    const char* name;
    int x;
    int y;
    int w;
    int h;
    bool toggleOnly;
};

constexpr int kOptionsTouchPadX = 20;
constexpr int kOptionsTouchPadY = 18;

// IMPORTANTE: estas coordenadas son los mismos rectángulos que se pintan en
// RadarRenderer::renderOptionsScreen(). Se usan en formato x/y/w/h y el borde
// derecho/inferior es exclusivo: x <= px < x+w, y <= py < y+h.
constexpr OptionsTouchBox kOptionsDistanceBox   {"DISTANCE",     58,  86, 164, 46, false};
constexpr OptionsTouchBox kOptionsSweepBox      {"SWEEP/API",   258,  86, 164, 46, false};
constexpr OptionsTouchBox kOptionsBorderBox     {"RADAR COLOR",  58, 144, 164, 46, false};
constexpr OptionsTouchBox kOptionsBrightnessBox {"BRIGHTNESS",  258, 144, 164, 46, false};
constexpr OptionsTouchBox kOptionsAlarmBox      {"ALARM",        58, 202, 164, 46, true};
constexpr OptionsTouchBox kOptionsNorthBeepBox  {"NORTH BEEP",  258, 202, 164, 46, true};
constexpr OptionsTouchBox kOptionsHourBox       {"HOUR",         58, 260, 164, 46, false};
constexpr OptionsTouchBox kOptionsMinuteBox     {"MIN",         258, 260, 164, 46, false};
constexpr OptionsTouchBox kOptionsRadarFpsBox   {"RADAR FPS",    58, 318, 164, 46, false};
constexpr OptionsTouchBox kOptionsGyroFpsBox    {"GYRO FPS",    258, 318, 164, 46, false};
// Mismas coordenadas que RadarRenderer::renderOptionsScreen().
// Layout inferior: WIFI | MIL | DIM, manteniendo el aspecto oscuro de la versión alternativa.
constexpr OptionsTouchBox kOptionsWifiBox       {"WIFI",  68, 384, 96, 38, true};
constexpr OptionsTouchBox kOptionsMilBox        {"MIL",  192, 384, 96, 38, true};
constexpr OptionsTouchBox kOptionsDimBox        {"DIM",  316, 384, 96, 38, true};
constexpr int kPomodoroButtonPad = 18;
constexpr int kPomodoroStartX = 68;
constexpr int kPomodoroStartY = 318;
constexpr int kPomodoroStartW = 156;
constexpr int kPomodoroStartH = 56;
constexpr int kPomodoroResetX = 256;
constexpr int kPomodoroResetY = 318;
constexpr int kPomodoroResetW = 156;
constexpr int kPomodoroResetH = 56;
constexpr int kPomodoroCardPad = 14;
constexpr int kPomodoroFocusX = 50;
constexpr int kPomodoroFocusY = 124;
constexpr int kPomodoroBreakX = 260;
constexpr int kPomodoroBreakY = 124;
constexpr int kPomodoroLongBreakX = 50;
constexpr int kPomodoroLongBreakY = 226;
constexpr int kPomodoroSoundX = 260;
constexpr int kPomodoroSoundY = 226;
constexpr int kPomodoroWaterX = 50;
constexpr int kPomodoroWaterY = 328;
constexpr int kPomodoroEyesX = 260;
constexpr int kPomodoroEyesY = 328;
constexpr int kPomodoroCardW = 170;
constexpr int kPomodoroCardH = 78;

constexpr const OptionsTouchBox* kOptionsTouchBoxes[] = {
    &kOptionsDistanceBox,
    &kOptionsBorderBox,
    &kOptionsBrightnessBox,
    &kOptionsSweepBox,
    &kOptionsHourBox,
    &kOptionsMinuteBox,
    &kOptionsRadarFpsBox,
    &kOptionsGyroFpsBox,
    &kOptionsAlarmBox,
    &kOptionsNorthBeepBox,
    &kOptionsWifiBox,
    &kOptionsMilBox,
    &kOptionsDimBox,
};

bool pointInsideOptionsBox(int px, int py, const OptionsTouchBox& box)
{
    return px >= box.x && px < (box.x + box.w)
        && py >= box.y && py < (box.y + box.h);
}

bool pointInsideOptionsBoxPadded(int px, int py, const OptionsTouchBox& box)
{
    return px >= (box.x - kOptionsTouchPadX)
        && px < (box.x + box.w + kOptionsTouchPadX)
        && py >= (box.y - kOptionsTouchPadY)
        && py < (box.y + box.h + kOptionsTouchPadY);
}

int optionsBoxCenterDistanceSq(int px, int py, const OptionsTouchBox& box)
{
    const int centerX = box.x + (box.w / 2);
    const int centerY = box.y + (box.h / 2);
    const int dx = px - centerX;
    const int dy = py - centerY;
    return (dx * dx) + (dy * dy);
}

const OptionsTouchBox* findOptionsTouchBox(int x, int y)
{
    for (const OptionsTouchBox* box : kOptionsTouchBoxes) {
        if (pointInsideOptionsBox(x, y, *box)) {
            return box;
        }
    }

    const OptionsTouchBox* bestBox = nullptr;
    int bestDistance = 0;
    for (const OptionsTouchBox* box : kOptionsTouchBoxes) {
        if (!pointInsideOptionsBoxPadded(x, y, *box)) {
            continue;
        }
        const int distance = optionsBoxCenterDistanceSq(x, y, *box);
        if (bestBox == nullptr || distance < bestDistance) {
            bestBox = box;
            bestDistance = distance;
        }
    }
    if (bestBox != nullptr) {
        return bestBox;
    }
    return nullptr;
}

int optionsDeltaForPoint(int x, const OptionsTouchBox& box)
{
    const int splitX = box.x + (box.w / 2);
    return x < splitX ? -1 : 1;
}

bool pointInsidePaddedRect(int px, int py, int x, int y, int w, int h, int pad)
{
    return px >= (x - pad)
        && px < (x + w + pad)
        && py >= (y - pad)
        && py < (y + h + pad);
}

float trafficFetchRangeKmForTheme(float displayRangeKm, uint8_t radarTheme)
{
    float fetchRangeKm = displayRangeKm;
    if (radarTheme == AppConfig::kRadarThemeDragonBall) {
        fetchRangeKm = std::min(fetchRangeKm * 3.0f, 200.0f);
    }
    return fetchRangeKm;
}

void handleRootThunk()
{
    if (g_activeRadarApp != nullptr) {
        g_activeRadarApp->handleRootRequest();
    }
}

void handleSaveThunk()
{
    if (g_activeRadarApp != nullptr) {
        g_activeRadarApp->handleSaveRequest();
    }
}


void IRAM_ATTR handleTouchInterruptThunk()
{
    g_touchInterruptPending = true;
}

float angleDistanceRadians(float left, float right)
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.0f;
    float delta = fabsf(left - right);
    while (delta > kTwoPi) {
        delta -= kTwoPi;
    }
    return delta > kPi ? kTwoPi - delta : delta;
}

uint32_t frameIntervalMsForFps(uint8_t fps)
{
    return 1000UL / static_cast<uint32_t>(fps == 0 ? 1 : fps);
}

}

const char* radarThemeName(uint8_t theme)
{
    switch (theme) {
        case AppConfig::kRadarThemeGreen: return "Green";
        case AppConfig::kRadarThemeBlue: return "Blue";
        case AppConfig::kRadarThemeCyan: return "Cyan";
        case AppConfig::kRadarThemeRed: return "Red";
        case AppConfig::kRadarThemeDragonBall: return "Dragon Ball";
    }
    return "Green";
}

RadarApp::RadarApp(esp_panel::board::Board* board)
    : m_board(board)
    , m_model(AppConfig::kOwnshipLat, AppConfig::kOwnshipLon, AppConfig::kRangeKm)
{
    g_activeRadarApp = this;
}

RadarApp::~RadarApp()
{
    detachInterrupt(digitalPinToInterrupt(kTouchInterruptPin));
    ImageSource::end();
    if (m_partialUploadBuffer != nullptr) {
        heap_caps_free(m_partialUploadBuffer);
        m_partialUploadBuffer = nullptr;
    }
    if (g_activeRadarApp == this) {
        g_activeRadarApp = nullptr;
    }
}

bool RadarApp::begin()
{
    m_lcd = m_board->getLCD();
    m_touch = m_board->getTouch();

    if (m_lcd == nullptr) {
        RADAR_LOG_PRINTLN("ERROR: RadarApp LCD nullptr");
        return false;
    }

    const size_t fbSize = AppConfig::kScreenWidth * AppConfig::kScreenHeight * sizeof(lv_color_t);
    RADAR_LOG_PRINTF("PSRAM size: %u  Free: %u  Heap: %u  FB: %u\n",
                  ESP.getPsramSize(), ESP.getFreePsram(), ESP.getFreeHeap(),
                  static_cast<unsigned>(fbSize));

    m_lcdBuffer[0] = static_cast<lv_color_t*>(m_lcd->getFrameBufferByIndex(0));
    m_lcdBuffer[1] = static_cast<lv_color_t*>(m_lcd->getFrameBufferByIndex(1));
    m_usePanelFrameBuffers = (m_lcdBuffer[0] != nullptr) && (m_lcdBuffer[1] != nullptr);

    lv_color_t* fb = nullptr;
    if (m_usePanelFrameBuffers) {
        RADAR_LOG_PRINTLN("Using panel double framebuffer");
        memset(m_lcdBuffer[0], 0, fbSize);
        memset(m_lcdBuffer[1], 0, fbSize);
        m_renderBufferIndex = 0;
        fb = m_lcdBuffer[m_renderBufferIndex];
    } else {
        RADAR_LOG_PRINTLN("Panel framebuffer unavailable, using PSRAM render buffer");
        fb = static_cast<lv_color_t*>(
            heap_caps_malloc(fbSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (fb == nullptr) {
            RADAR_LOG_PRINTLN("ERROR: could not allocate framebuffer in PSRAM");
            return false;
        }
        memset(fb, 0, fbSize);
    }
    m_renderer.attachBuffer(fb, AppConfig::kScreenWidth, AppConfig::kScreenHeight);

    if (m_touch != nullptr) {
        pinMode(kTouchInterruptPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(kTouchInterruptPin), handleTouchInterruptThunk, FALLING);
        m_touch->swapXY(false);
        m_touch->mirrorX(false);
        m_touch->mirrorY(false);
    }

    m_configStore.begin(*m_board);
    const bool configLoaded = m_configStore.load(m_runtimeConfig);
    m_runtimeConfig.normalize();
    setenv("TZ", AppConfig::kTimezoneTz, 1);
    tzset();
    applyRuntimeConfigToSystems();
    m_imu.begin();
    m_rtc.begin();
    restoreTimeFromRtc();
    ImageSource::reserveMemory();
    m_pomodoroFocusMinutes = m_runtimeConfig.pomodoroFocusMinutes;
    m_pomodoroBreakMinutes = m_runtimeConfig.pomodoroBreakMinutes;
    m_pomodoroLongBreakMinutes = m_runtimeConfig.pomodoroLongBreakMinutes;
    m_waterReminderMinutes = m_runtimeConfig.waterReminderMinutes;
    m_stretchReminderMinutes = m_runtimeConfig.stretchReminderMinutes;
    m_pomodoroSoundEnabled = m_runtimeConfig.pomodoroSoundEnabled;
    m_pomodoroRemainingMs = pomodoroPhaseDurationMs();
    setBrightness(m_runtimeConfig.brightness);
    m_lastUserActivityMs = millis();
    m_startupAnimationStartMs = m_lastUserActivityMs;
    m_startupAnimationDone = !AppConfig::kStartupAnimationEnabled;
    initAlarmOutput();
    m_lcd->setDisplayOnOff(true);
    m_viewMode = ViewMode::Radar;
    m_nextApiFetchMs = 0;

    if (configLoaded && m_runtimeConfig.hasWiFi()) {
        RADAR_LOG_PRINTLN("Starting radar mode");
        startRadarMode();
    } else {
        RADAR_LOG_PRINTLN("Starting setup portal");
        startSetupPortal();
    }

    return true;
}

void RadarApp::applyRuntimeConfigToSystems()
{
    m_runtimeConfig.normalize();
    m_model.setOwnship(m_runtimeConfig.ownshipLat, m_runtimeConfig.ownshipLon, m_runtimeConfig.rangeKm);
    m_model.setSweepPeriodSeconds(m_runtimeConfig.apiRefreshSeconds);
    m_model.setFetchRangeKm(trafficFetchRangeKmForTheme(m_runtimeConfig.rangeKm, m_runtimeConfig.radarTheme));
}

void RadarApp::persistRuntimeConfig()
{
    m_runtimeConfig.pomodoroFocusMinutes = m_pomodoroFocusMinutes;
    m_runtimeConfig.pomodoroBreakMinutes = m_pomodoroBreakMinutes;
    m_runtimeConfig.pomodoroLongBreakMinutes = m_pomodoroLongBreakMinutes;
    m_runtimeConfig.waterReminderMinutes = m_waterReminderMinutes;
    m_runtimeConfig.stretchReminderMinutes = m_stretchReminderMinutes;
    m_runtimeConfig.pomodoroSoundEnabled = m_pomodoroSoundEnabled;
    m_runtimeConfig.normalize();
    m_configSavePending = true;
    m_configSaveRequestedMs = millis();
}

void RadarApp::requestRuntimeConfigSave(uint32_t nowMs)
{
    if (!m_configSavePending) {
        m_configSavePending = true;
        m_configSaveRequestedMs = nowMs;
    }
    if (m_configSaveModalActive) {
        return;
    }
    if (m_mode == Mode::Radar) {
        m_configSaveModalActive = true;
        m_configSaveModalAttempted = false;
        m_configSaveModalOk = false;
        m_configSaveModalStartMs = nowMs;
        m_configSaveModalUntilMs = nowMs + kRuntimeConfigSaveModalMinMs;
        m_configSaveModalLastRenderMs = 0;
        m_touchIgnoreUntilMs = nowMs + kRuntimeConfigSaveModalMinMs + kRuntimeConfigSaveTouchSettleMs;
        m_lastRenderMs = 0;
    }
}

bool RadarApp::isRuntimeConfigEditView(ViewMode view) const
{
    return view == ViewMode::Options || view == ViewMode::PomodoroSettings;
}

bool RadarApp::saveRuntimeConfigNow()
{
    m_runtimeConfig.pomodoroFocusMinutes = m_pomodoroFocusMinutes;
    m_runtimeConfig.pomodoroBreakMinutes = m_pomodoroBreakMinutes;
    m_runtimeConfig.pomodoroLongBreakMinutes = m_pomodoroLongBreakMinutes;
    m_runtimeConfig.waterReminderMinutes = m_waterReminderMinutes;
    m_runtimeConfig.stretchReminderMinutes = m_stretchReminderMinutes;
    m_runtimeConfig.pomodoroSoundEnabled = m_pomodoroSoundEnabled;
    m_runtimeConfig.normalize();

    // Cerramos solo el archivo .anim activo. La SD sigue montada una sola vez
    // mediante SharedSd; desmontarla aqui podia bloquear FatFS durante el save.
    ImageSource::suspend();

    if (!m_configStore.save(m_runtimeConfig)) {
        return false;
    }

    m_configSavePending = false;
    m_configSaveRequestedMs = 0;
    m_configSaveLastAttemptMs = 0;
    return true;
}

void RadarApp::updateRuntimeConfigSave(uint32_t nowMs)
{
    if (m_configSaveModalActive) {
        return;
    }

    if (!m_configSavePending) {
        return;
    }

    if (static_cast<int32_t>(nowMs - m_configSaveRequestedMs) < static_cast<int32_t>(kRuntimeConfigSaveDebounceMs)) {
        return;
    }

    if (m_mode != Mode::Radar || m_planePopupOpen) {
        return;
    }

    if (isRuntimeConfigEditView(m_viewMode)) {
        return;
    }

    if (m_configSaveLastAttemptMs != 0
        && (nowMs - m_configSaveLastAttemptMs) < kRuntimeConfigSaveRetryMs) {
        return;
    }

    requestRuntimeConfigSave(nowMs);
}

void RadarApp::updateConfigSaveModal(uint32_t nowMs)
{
    if (!m_configSaveModalActive) {
        return;
    }

    uint32_t currentMs = millis();
    uint32_t elapsedMs = currentMs - m_configSaveModalStartMs;
    const bool shouldRender = m_configSaveModalLastRenderMs == 0
        || (currentMs - m_configSaveModalLastRenderMs) >= kRuntimeConfigSaveModalFrameMs;
    if (shouldRender) {
        m_configSaveModalLastRenderMs = currentMs;
        m_renderer.renderStorageSaveScreen(
            m_configSaveModalAttempted,
            m_configSaveModalOk,
            elapsedMs,
            kRuntimeConfigSaveModalMinMs,
            m_runtimeConfig.radarTheme);
        flushDisplay();
    }

    if (!m_configSaveModalAttempted
        && static_cast<int32_t>(currentMs - m_configSaveModalStartMs) >= static_cast<int32_t>(kRuntimeConfigSaveModalPreSaveMs)) {
        m_configSaveLastAttemptMs = currentMs;
        m_configSaveModalOk = saveRuntimeConfigNow();
        m_configSaveModalAttempted = true;
        currentMs = millis();
        elapsedMs = currentMs - m_configSaveModalStartMs;
        if (!m_configSaveModalOk) {
            RADAR_LOG_PRINTLN("WARN: could not save config to SD during modal save; keeping RAM config");
        }
        const uint32_t minimumUntilMs = m_configSaveModalStartMs + kRuntimeConfigSaveModalMinMs;
        const uint32_t resultVisibleUntilMs = currentMs + 850;
        m_configSaveModalUntilMs = std::max(minimumUntilMs, resultVisibleUntilMs);
        m_configSaveModalLastRenderMs = currentMs;
        m_renderer.renderStorageSaveScreen(
            true,
            m_configSaveModalOk,
            elapsedMs,
            kRuntimeConfigSaveModalMinMs,
            m_runtimeConfig.radarTheme);
        flushDisplay();
    }

    currentMs = millis();
    if (m_configSaveModalAttempted
        && static_cast<int32_t>(currentMs - m_configSaveModalUntilMs) >= 0) {
        m_configSaveModalActive = false;
        m_configSaveModalAttempted = false;
        m_configSaveModalLastRenderMs = 0;
        m_touchWasPressed = false;
        m_touchConsumed = true;
        m_planePopupTapPending = false;
        m_touchIgnoreUntilMs = currentMs + kRuntimeConfigSaveTouchSettleMs;
        m_lastRenderMs = 0;
        m_renderer.forceNextRadarFullFrame();
        return;
    }

    updateIdleBrightness(currentMs);
    vTaskDelay(pdMS_TO_TICKS(20));
}

void RadarApp::loop()
{
    const uint32_t nowMs = millis();

    if (m_restartRequested) {
        if (m_configSavePending) {
            saveRuntimeConfigNow();
        }
        ESP.restart();
    }

    if (!m_startupAnimationDone) {
        if (m_startupAnimationStartMs == 0) {
            m_startupAnimationStartMs = nowMs;
        }
        const uint32_t elapsedMs = nowMs - m_startupAnimationStartMs;
        if (elapsedMs < AppConfig::kStartupAnimationMs) {
            const uint32_t startupFrameMs = frameIntervalMsForFps(AppConfig::kStartupAnimationFps);
            if (m_lastRenderMs == 0 || (nowMs - m_lastRenderMs) >= startupFrameMs) {
                m_lastRenderMs = nowMs;
                m_renderer.renderStartupScreen(elapsedMs, AppConfig::kStartupAnimationMs, m_runtimeConfig.radarTheme);
                flushDisplay();
            }
            updateIdleBrightness(nowMs);
            vTaskDelay(pdMS_TO_TICKS(1));
            return;
        }
        m_startupAnimationDone = true;
        m_lastRenderMs = 0;
        m_renderer.forceNextRadarFullFrame();
    }

    if (m_configSaveModalActive) {
        updateConfigSaveModal(nowMs);
        return;
    }

    // Mientras se decide un toque sobre un avión (m_planePopupTapPending) sondeamos
    // el táctil más rápido para que la ventana de decisión se muestree varias veces
    // y el detalle del avión se abra casi al instante, sin esperar al siguiente
    // sondeo lento de 20 ms.
    const bool directTouchView = m_mode == Mode::Radar
        && (m_viewMode == ViewMode::Options
            || m_viewMode == ViewMode::Image
            || m_viewMode == ViewMode::PomodoroControl
            || m_viewMode == ViewMode::PomodoroSettings);
    const uint32_t touchPollIntervalMs = directTouchView
        || m_planePopupOpen
        || m_planePopupTapPending
        ? kOptionsTouchPollIntervalMs
        : AppConfig::kTouchPollIntervalMs;
    if (nowMs - m_lastTouchPollMs >= touchPollIntervalMs) {
        m_lastTouchPollMs = nowMs;
        handleTouch();
    }
    updateRuntimeConfigSave(nowMs);

    if (m_mode == Mode::SetupPortal) {
        m_dnsServer.processNextRequest();
        m_webServer.handleClient();

        if (nowMs - m_lastRenderMs >= AppConfig::kPortalRenderMs) {
            m_lastRenderMs = nowMs;
            renderCurrentMode();
        }
        updateIdleBrightness(nowMs);
        return;
    }

    if (m_mode == Mode::Radar) {
        const bool radarView = m_viewMode == ViewMode::Radar;
        const bool clockView = m_viewMode == ViewMode::Clock;
        const bool skyView = m_viewMode == ViewMode::Sky;
        const bool watchView = m_viewMode == ViewMode::Watch;
        const bool alternativeClockView = m_viewMode == ViewMode::AlternativeClock;
        const bool dayClockView = m_viewMode == ViewMode::DayClock;
        const bool imageView = m_viewMode == ViewMode::Image;
        const bool pomodoroView = m_viewMode == ViewMode::PomodoroControl
            || m_viewMode == ViewMode::PomodoroSettings;
        bool fetchCompleted = false;

        if (m_hasPendingFetch && m_fetchMutex != nullptr) {
            if (xSemaphoreTake(m_fetchMutex, 0) == pdTRUE) {
                FetchResult result = std::move(m_pendingFetchResult);
                m_hasPendingFetch = false;
                xSemaphoreGive(m_fetchMutex);
                fetchCompleted = true;

                if (result.ok) {
                    m_consecutiveFetchFailures = 0;
                    m_trafficApiFailureVisible = false;
                    m_model.setFetchRangeKm(trafficFetchRangeKmForTheme(m_runtimeConfig.rangeKm, m_runtimeConfig.radarTheme));
                    m_model.applyFetchResult(result, m_runtimeConfig.militaryOnlyEnabled);
                    RADAR_LOG_PRINTF("Traffic model: pending=%u visible=%u apiOk=%d range=%.1f fetchRange=%.1f\n",
                        static_cast<unsigned>(m_model.pendingPlanes().size()),
                        static_cast<unsigned>(m_model.visiblePlanes().size()),
                        m_model.apiOk() ? 1 : 0,
                        m_model.rangeKm(),
                        m_model.fetchRangeKm());
                } else {
                    const bool wifiOnlyFailure = result.statusText.indexOf("WiFi not connected") >= 0;
                    if (!wifiOnlyFailure) {
                        if (m_consecutiveFetchFailures < 255) {
                            ++m_consecutiveFetchFailures;
                        }
                        if (m_consecutiveFetchFailures >= 3) {
                            m_trafficApiFailureVisible = true;
                            m_model.setFetchRangeKm(trafficFetchRangeKmForTheme(m_runtimeConfig.rangeKm, m_runtimeConfig.radarTheme));
                            m_model.applyFetchResult(result, m_runtimeConfig.militaryOnlyEnabled);
                        }
                    }
                }
            }
        }

        if (m_hasPendingWeather && m_fetchMutex != nullptr) {
            if (xSemaphoreTake(m_fetchMutex, 0) == pdTRUE) {
                WeatherResult result = std::move(m_pendingWeatherResult);
                m_hasPendingWeather = false;
                xSemaphoreGive(m_fetchMutex);

                if (result.ok) {
                    m_dayClockWeather = result;
                    m_nextDayClockWeatherFetchMs = nowMs + kDayClockWeatherRefreshMs;
                    if (dayClockView) {
                        m_lastRenderMs = 0;
                    }
                } else {
                    m_dayClockWeather.statusText = result.statusText;
                    m_nextDayClockWeatherFetchMs = nowMs + kDayClockWeatherRetryMs;
                    if (dayClockView) {
                        m_lastRenderMs = 0;
                    }
                }
            }
        }

        if (clockView) {
            suspendWiFiForClock();
            updateClockImu(nowMs);
        } else if (skyView || watchView || alternativeClockView || imageView || pomodoroView) {
            suspendWiFiForClock();
        } else if (dayClockView) {
            if (m_runtimeConfig.hasWiFi()
                && (!m_dayClockWeather.ok || nowMs >= m_nextDayClockWeatherFetchMs)) {
                resumeWiFiAfterClock();
            } else {
                suspendWiFiForClock();
            }
        }

        const bool dragonBallRadar = radarView
            && m_runtimeConfig.radarTheme == AppConfig::kRadarThemeDragonBall;
        // El radar normal ya no lanza peticiones por temporizador continuo.
        // El snapshot de tráfico se pide al cruzar el norte. Unos segundos antes
        // desactivamos el ahorro WiFi para que el enlace llegue despierto al fetch;
        // después de la consulta volvemos al modo WiFi power-save.
        if (radarView && !dragonBallRadar && !m_planePopupOpen) {
            prepareRadarWiFiForNorth(nowMs);
        }

        if (dayClockView && m_mode == Mode::Radar
            && m_runtimeConfig.hasWiFi()
            && !m_dayClockWeather.ok
            && !m_fetchInProgress
            && m_networkTaskHandle == nullptr
            && (m_nextDayClockWeatherFetchMs == 0 || nowMs >= m_nextDayClockWeatherFetchMs)
            && !m_networkRequestWeather) {
            m_dayClockWeatherRequested = true;
            scheduleNetworkFetch(nowMs, kDayClockWeatherRefreshMs, false, true);
        }

        updateClockAlarm();
        updateBuzzerCue(nowMs);
        if (m_lastPomodoroUpdateMs == 0
            || (nowMs - m_lastPomodoroUpdateMs) >= kPomodoroUpdateIntervalMs) {
            m_lastPomodoroUpdateMs = nowMs;
            updatePomodoro(nowMs);
        }
        updateDragonBallTouchBeep(nowMs);
        const bool dragonBallActive = dragonBallRadar && m_renderer.dragonBallScanActive(nowMs);
        const bool dragonBallJustEnded = dragonBallRadar && m_dragonBallRenderWasActive && !dragonBallActive;
        m_dragonBallRenderWasActive = dragonBallRadar && dragonBallActive;

        uint32_t frameIntervalMs = frameIntervalMsForFps(m_runtimeConfig.fps);
        // La pose del IMU se lee una sola vez por frame y se reutiliza tanto para
        // decidir si hay movimiento como para guardar la pose ya renderizada.
        float clockTiltX = 0.0f;
        float clockTiltY = 0.0f;
        float clockRotation = 0.0f;
        if (clockView) {
            if (m_imu.available()) {
                clockTiltX = m_imu.tiltX();
                clockTiltY = m_imu.tiltY();
                clockRotation = m_imu.rotationRad();
            }
            const bool moving = !m_hasClockRenderPose
                || fabsf(clockTiltX - m_lastClockRenderTiltX) > kClockTiltRenderThreshold
                || fabsf(clockTiltY - m_lastClockRenderTiltY) > kClockTiltRenderThreshold
                || angleDistanceRadians(clockRotation, m_lastClockRenderRotation) > kClockRotationRenderThresholdRad;
            m_clockMotionActive = moving;
            if (moving) {
                m_lastClockMotionMs = nowMs;
                if (m_hasClockRenderPose) {
                    registerUserActivity(nowMs);
                }
            }
            frameIntervalMs = moving ? frameIntervalMsForFps(m_runtimeConfig.gyroFps) : kClockIdleFrameMs;
        } else if (watchView) {
            frameIntervalMs = kWatchFrameMs;
        } else if (dayClockView) {
            frameIntervalMs = kWatchFrameMs;
        } else if (imageView) {
            frameIntervalMs = frameIntervalMsForFps(m_runtimeConfig.gyroFps);
            ImageSource::setFrameIntervalMs(frameIntervalMs);
        } else if (pomodoroView) {
            frameIntervalMs = kPomodoroFrameMs;
        } else if (skyView || alternativeClockView) {
            frameIntervalMs = kSkyFrameMs;
        } else if (dragonBallRadar) {
            frameIntervalMs = dragonBallActive
                ? frameIntervalMsForFps(m_runtimeConfig.fps)
                : 1000;
        } else if (m_planePopupOpen && radarView) {
            frameIntervalMs = 500;
        }

        // La animación de inicio se renderiza antes de entrar en las vistas normales,
        // así que aquí no necesitamos forzar FPS especial del radar.
        constexpr bool bootAnimationHighFps = false;

        bool shouldRender = m_lastRenderMs == 0 || (nowMs - m_lastRenderMs) >= frameIntervalMs;
        if (m_planePopupOpen && radarView) {
            shouldRender = (m_lastRenderMs == 0);
        }
        if (m_viewMode == ViewMode::Options) {
            // Pantallas estáticas: solo se pintan cuando un toque o un cambio de vista
            // fuerza el render poniendo m_lastRenderMs = 0.
            shouldRender = (m_lastRenderMs == 0);
        } else if (m_viewMode == ViewMode::PomodoroSettings) {
            shouldRender = (m_lastRenderMs == 0);
        } else if (dayClockView) {
            // Hoy/DayClock: solo se pinta al entrar (m_lastRenderMs == 0) o cuando cambia el minuto
            int currentMin = -1;
            const time_t now = time(nullptr);
            tm localInfo{};
            if (now > 100000 && localtime_r(&now, &localInfo) != nullptr) {
                currentMin = localInfo.tm_min;
            }
            shouldRender = (m_lastRenderMs == 0) || (currentMin != m_lastDayClockMinuteKey);
        }
        if (dragonBallRadar) {
            const bool initialRender = m_lastRenderMs == 0;
            const bool apiPending = m_fetchInProgress || m_hasPendingFetch;
            const bool dragonBallHasWork = dragonBallActive || dragonBallJustEnded || apiPending || fetchCompleted || initialRender;
            shouldRender = dragonBallHasWork
                && (shouldRender || initialRender || dragonBallJustEnded || fetchCompleted);
        } else {
            m_dragonBallRenderWasActive = false;
        }

        if (shouldRender) {
            if (radarView && !m_planePopupOpen) {
                if (dragonBallRadar) {
                    m_model.tickNow(nowMs);
                } else {
                    m_model.tick(nowMs);
                    if (detectRadarNorthCrossing(m_model.sweepAngleDeg())) {
                        requestRadarSnapshotAtNorth(nowMs);
                    }
                }
                updateNorthBeep(nowMs, radarView);
            }
            m_lastRenderMs = nowMs;
            renderCurrentMode();
            if (clockView) {
                m_lastClockRenderTiltX = clockTiltX;
                m_lastClockRenderTiltY = clockTiltY;
                m_lastClockRenderRotation = clockRotation;
                m_hasClockRenderPose = true;
            }
        }

        updateIdleBrightness(nowMs);

        if (dayClockView) {
            vTaskDelay(pdMS_TO_TICKS(20));
        } else if (skyView || alternativeClockView) {
            // Vistas ligeras: dormimos m�s para ahorrar.
            vTaskDelay(pdMS_TO_TICKS(50));
        } else if (imageView) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else if (pomodoroView) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else if (watchView) {
            // Reloj anal�gico: cadencia corta para alimentar los ~10 FPS del arco.
            vTaskDelay(pdMS_TO_TICKS(20));
        } else if (m_viewMode == ViewMode::Options) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else if (clockView && !m_clockMotionActive) {
            vTaskDelay(pdMS_TO_TICKS(20));
        } else if (radarView && m_planePopupOpen) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else if (radarView) {
            uint32_t sleepMs = 5;
            if (bootAnimationHighFps) {
                sleepMs = 1;
            } else if (dragonBallRadar && !dragonBallActive) {
                sleepMs = 20;
            } else if (m_lastRenderMs != 0) {
                const uint32_t nextFrameMs = m_lastRenderMs + frameIntervalMs;
                if (nowMs < nextFrameMs) {
                    sleepMs = nextFrameMs - nowMs;
                }
            }
            const uint32_t maxSleepMs = AppConfig::kRadarLoopMaxSleepMs;
            if (sleepMs > maxSleepMs) {
                sleepMs = maxSleepMs;
            }
            if (sleepMs < 1) {
                sleepMs = 1;
            }
            vTaskDelay(pdMS_TO_TICKS(sleepMs));
        }
    }
}

void RadarApp::networkTaskEntry(void* context)
{
    auto* self = static_cast<RadarApp*>(context);
    self->networkTask();
    vTaskDelete(nullptr);
}

bool RadarApp::scheduleNetworkFetch(uint32_t nowMs, uint32_t nextDelayMs, bool requestTraffic, bool requestWeather)
{
    if (!requestTraffic && !requestWeather) {
        return false;
    }

    if (m_fetchMutex != nullptr) {
        xSemaphoreTake(m_fetchMutex, portMAX_DELAY);
        m_networkRequestTraffic = m_networkRequestTraffic || requestTraffic;
        m_networkRequestWeather = m_networkRequestWeather || requestWeather;
        xSemaphoreGive(m_fetchMutex);
    } else {
        m_networkRequestTraffic = m_networkRequestTraffic || requestTraffic;
        m_networkRequestWeather = m_networkRequestWeather || requestWeather;
    }

    if (m_fetchInProgress || m_networkTaskHandle != nullptr) {
        return true;
    }

    m_fetchInProgress = true;
    if (requestTraffic) {
        m_nextApiFetchMs = nowMs + nextDelayMs;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(networkTaskEntry, "net", 12288, this, 2, &m_networkTaskHandle, 0);
    if (created != pdPASS) {
        m_networkTaskHandle = nullptr;
        m_fetchInProgress = false;
        return false;
    }

    return true;
}

void RadarApp::networkTask()
{
    FetchResult result;
    result.sourceName = AppConfig::kTrafficSourceName;
    WeatherResult weatherResult;

    bool fetchTraffic = false;
    bool fetchWeather = false;
    if (m_fetchMutex != nullptr) {
        xSemaphoreTake(m_fetchMutex, portMAX_DELAY);
        fetchTraffic = m_networkRequestTraffic;
        fetchWeather = m_networkRequestWeather || m_dayClockWeatherRequested;
        m_networkRequestTraffic = false;
        m_networkRequestWeather = false;
        xSemaphoreGive(m_fetchMutex);
    } else {
        fetchTraffic = m_networkRequestTraffic;
        fetchWeather = m_networkRequestWeather || m_dayClockWeatherRequested;
        m_networkRequestTraffic = false;
        m_networkRequestWeather = false;
    }

    const bool needsNetwork = fetchTraffic || fetchWeather;
    if (needsNetwork) {
        setRadarWiFiActiveForSnapshot(true);
    }

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFiBlocking();
    }

    if (wifiStationReady()) {
        syncNetworkTimeIfDue(false);

        if (fetchTraffic) {
            const float fetchRangeKm = trafficFetchRangeKmForTheme(m_runtimeConfig.rangeKm, m_runtimeConfig.radarTheme);

            RADAR_LOG_PRINTF("Traffic: fetch lat=%.6f lon=%.6f range=%.1f\n",
                m_runtimeConfig.ownshipLat,
                m_runtimeConfig.ownshipLon,
                fetchRangeKm);
            result = m_trafficClient.fetchSnapshot(
                m_runtimeConfig.ownshipLat,
                m_runtimeConfig.ownshipLon,
                fetchRangeKm);
            RADAR_LOG_PRINTF("Traffic: %s planes=%u raw=%d valid=%d status=%s\n",
                result.ok ? "OK" : "FAIL",
                static_cast<unsigned>(result.planes.size()),
                result.rawStateCount,
                result.validPositionCount,
                result.statusText.c_str());
        }

        if (fetchWeather) {
            RADAR_LOG_PRINTF("Weather: fetch lat=%.6f lon=%.6f\n",
                m_runtimeConfig.ownshipLat,
                m_runtimeConfig.ownshipLon);
            weatherResult = m_trafficClient.fetchWeather(
                m_runtimeConfig.ownshipLat,
                m_runtimeConfig.ownshipLon);
            RADAR_LOG_PRINTF("Weather: %s temp=%.1f cond=%s status=%s\n",
                weatherResult.ok ? "OK" : "FAIL",
                weatherResult.temperatureC,
                weatherResult.conditionText.c_str(),
                weatherResult.statusText.c_str());
        }
    } else if (fetchTraffic) {
        result.ok = false;
        result.statusText = "WiFi not connected";
    } else if (fetchWeather) {
        weatherResult.ok = false;
        weatherResult.statusText = "WiFi not connected";
    }

    if (m_fetchMutex != nullptr) {
        xSemaphoreTake(m_fetchMutex, portMAX_DELAY);
        if (fetchTraffic) {
            m_pendingFetchResult = std::move(result);
            m_hasPendingFetch = true;
        }
        if (fetchWeather) {
            m_pendingWeatherResult = std::move(weatherResult);
            m_hasPendingWeather = true;
            m_dayClockWeatherRequested = false;
        }
        xSemaphoreGive(m_fetchMutex);
    }

    if (needsNetwork) {
        setRadarWiFiActiveForSnapshot(false);
    }

    m_networkTaskHandle = nullptr;
    m_fetchInProgress = false;
}

void RadarApp::applyStationPowerSaving()
{
    WiFi.setSleep(true);
    WiFi.setTxPower(WIFI_POWER_11dBm);
}

void RadarApp::suspendWiFiForClock()
{
    if (m_clockWifiSuspended || !m_runtimeConfig.hasWiFi()) {
        return;
    }
    if (m_fetchInProgress || m_networkTaskHandle != nullptr) {
        return;
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    m_clockWifiSuspended = true;
    m_radarWifiAwakeForSnapshot = false;
}

void RadarApp::resumeWiFiAfterClock()
{
    if (!m_clockWifiSuspended) {
        return;
    }

    m_clockWifiSuspended = false;
    if (m_runtimeConfig.hasWiFi()) {
        WiFi.mode(WIFI_STA);
        applyStationPowerSaving();
        m_nextApiFetchMs = millis();
    }
}

void RadarApp::updateClockImu(uint32_t nowMs)
{
    const bool recentlyMoving = !m_hasClockRenderPose
        || m_clockMotionActive
        || (nowMs - m_lastClockMotionMs) < kClockIdleImuDelayMs;
    const uint32_t intervalMs = recentlyMoving ? kClockFastImuUpdateMs : kClockIdleImuUpdateMs;
    if (nowMs - m_lastClockImuUpdateMs < intervalMs) {
        return;
    }

    m_lastClockImuUpdateMs = nowMs;
    m_imu.update();
}

bool RadarApp::wifiStationReady() const
{
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    const IPAddress ip = WiFi.localIP();
    return ip != IPAddress(0, 0, 0, 0);
}

void RadarApp::connectWiFiBlocking()
{
    if (!m_runtimeConfig.hasWiFi()) {
        m_wifiConnectStatus[0] = '\0';
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(100);

    RADAR_LOG_PRINTLN("WiFi: perfiles en memoria:");
    int profileOrder[AppConfig::kMaxWifiProfiles] = {0, 1, 2};
    int profileRssi[AppConfig::kMaxWifiProfiles] = {kWifiNotVisibleRssi, kWifiNotVisibleRssi, kWifiNotVisibleRssi};
    int profileCount = 0;
    for (int i = 0; i < AppConfig::kMaxWifiProfiles; ++i) {
        if (m_runtimeConfig.wifiSsid[i].isEmpty()) {
            continue;
        }
        profileOrder[profileCount] = i;
        profileRssi[profileCount] = kWifiNotVisibleRssi;
        RADAR_LOG_PRINTF(
            "  WiFi %d: SSID=%s pass=%s\n",
            i + 1,
            m_runtimeConfig.wifiSsid[i].c_str(),
            m_runtimeConfig.wifiPassword[i].isEmpty() ? "VACIA" : "ok");
        ++profileCount;
    }

    if (profileCount == 0) {
        snprintf(m_wifiConnectStatus, sizeof(m_wifiConnectStatus), "sin SSID guardado");
        return;
    }

    RADAR_LOG_PRINTLN("WiFi: escaneando redes cercanas...");
    const int scanCount = WiFi.scanNetworks(false, true, false, kWiFiScanMaxMs);
    RADAR_LOG_PRINTF("WiFi: escaneo devolvio %d redes\n", scanCount);
    for (int slot = 0; slot < profileCount; ++slot) {
        const int profileIndex = profileOrder[slot];
        profileRssi[slot] = scanRssiForSsid(m_runtimeConfig.wifiSsid[profileIndex], scanCount);
        RADAR_LOG_PRINTF(
            "  orden candidato WiFi %d (%s) RSSI=%d\n",
            profileIndex + 1,
            m_runtimeConfig.wifiSsid[profileIndex].c_str(),
            profileRssi[slot]);
    }
    WiFi.scanDelete();
    sortWifiProfilesByScan(profileOrder, profileRssi, profileCount);

    const int lastProfileIndex = m_configStore.loadLastWifiProfileIndex();
    promoteWifiProfileToFront(profileOrder, profileRssi, profileCount, lastProfileIndex);
    if (profileCount > 0 && profileOrder[0] == lastProfileIndex) {
        RADAR_LOG_PRINTF("WiFi: priorizando ultimo perfil valido W%d\n", lastProfileIndex + 1);
    }

    for (int attemptIndex = 0; attemptIndex < profileCount; ++attemptIndex) {
        const int profileIndex = profileOrder[attemptIndex];
        if (connectWiFiProfile(profileIndex, kWiFiProfileConnectTimeoutMs)) {
            // RAM-only: no escribimos el ultimo perfil WiFi en SD/NVS.
            m_wifiConnectStatus[0] = '\0';
            applyStationPowerSaving();
            syncNetworkTimeIfDue(true);
            return;
        }
    }

    applyStationPowerSaving();
    if (profileCount >= 2) {
        snprintf(
            m_wifiConnectStatus,
            sizeof(m_wifiConnectStatus),
            "fallo: borra WiFi1 si no usas");
    } else {
        snprintf(m_wifiConnectStatus, sizeof(m_wifiConnectStatus), "WiFi no conecta");
    }
    RADAR_LOG_PRINTLN("WiFi: ningun perfil guardado conecto");
}

bool RadarApp::restoreTimeFromRtc()
{
    if (!m_rtc.available()) {
        return false;
    }

    time_t rtcEpoch = 0;
    if (!m_rtc.readTime(rtcEpoch)) {
        RADAR_LOG_PRINTLN("RTC time invalid, waiting for NTP");
        return false;
    }

    timeval tv{};
    tv.tv_sec = rtcEpoch;
    if (settimeofday(&tv, nullptr) != 0) {
        RADAR_LOG_PRINTLN("WARN: settimeofday from RTC failed");
        return false;
    }

    RADAR_LOG_PRINTF("System time restored from RTC: %lld\n", static_cast<long long>(rtcEpoch));
    return true;
}

bool RadarApp::syncNetworkTimeIfDue(bool force)
{
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    const uint32_t nowMs = millis();
    if (!force && m_lastNtpSyncMs != 0 && (nowMs - m_lastNtpSyncMs) < kNtpResyncMs) {
        return false;
    }
    if (!force && m_lastNtpAttemptMs != 0 && (nowMs - m_lastNtpAttemptMs) < kNtpRetryMs) {
        return false;
    }

    m_lastNtpAttemptMs = nowMs;
    configTzTime(AppConfig::kTimezoneTz, "pool.ntp.org");

    const uint32_t startMs = millis();
    while (!systemTimeValid() && (millis() - startMs) < kNtpWaitMs) {
        delay(100);
    }

    if (!systemTimeValid()) {
        RADAR_LOG_PRINTLN("WARN: NTP sync did not provide valid time");
        return false;
    }

    m_lastNtpSyncMs = millis();
    syncRtcFromSystemTime();
    return true;
}

bool RadarApp::syncRtcFromSystemTime()
{
    if (!m_rtc.available() || !systemTimeValid()) {
        return false;
    }

    const time_t systemEpoch = time(nullptr);
    time_t rtcEpoch = 0;
    if (m_rtc.readTime(rtcEpoch) && llabs(static_cast<long long>(systemEpoch - rtcEpoch)) < 2) {
        return true;
    }

    if (!m_rtc.writeTime(systemEpoch)) {
        RADAR_LOG_PRINTLN("WARN: could not update PCF85063 RTC");
        return false;
    }

    RADAR_LOG_PRINTF("PCF85063 RTC updated from system time: %lld\n", static_cast<long long>(systemEpoch));
    return true;
}

bool RadarApp::systemTimeValid() const
{
    return time(nullptr) >= kValidTimeEpoch;
}

bool RadarApp::connectWiFiProfile(int profileIndex, uint32_t timeoutMs)
{
  if (profileIndex < 0 || profileIndex >= AppConfig::kMaxWifiProfiles
      || m_runtimeConfig.wifiSsid[profileIndex].isEmpty())
  {
    return false;
  }

  const String& ssid = m_runtimeConfig.wifiSsid[profileIndex];
  if (m_runtimeConfig.wifiPassword[profileIndex].isEmpty())
  {
    RADAR_LOG_PRINTF("WiFi: perfil %d sin contrasena (%s)\n", profileIndex + 1, ssid.c_str());
  }

  snprintf(
    m_wifiConnectStatus,
    sizeof(m_wifiConnectStatus),
    "TRY W%d",
    profileIndex + 1);
  RADAR_LOG_PRINTF(
    "WiFi: probando perfil %d SSID=%s timeout=%lu ms\n",
    profileIndex + 1,
    ssid.c_str(),
    static_cast<unsigned long>(timeoutMs));

  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid.c_str(), m_runtimeConfig.wifiPassword[profileIndex].c_str());

  const uint32_t startMs = millis();
  while ((millis() - startMs) < timeoutMs)
  {
    if (wifiStationReady())
    {
      RADAR_LOG_PRINTF(
        "WiFi: conectado perfil %d IP=%s\n",
        profileIndex + 1,
        WiFi.localIP().toString().c_str());
      return true;
    }

    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL)
    {
      RADAR_LOG_PRINTF(
        "WiFi: abandono perfil %d (estado=%d)\n",
        profileIndex + 1,
        static_cast<int>(status));
      break;
    }

    delay(kWiFiProfilePollMs);
  }

  RADAR_LOG_PRINTF(
    "WiFi: fallo perfil %d (estado=%d)\n",
    profileIndex + 1,
    static_cast<int>(WiFi.status()));
  return false;
}

bool RadarApp::startSetupPortal()
{
    m_mode = Mode::SetupPortal;
    // Reinicio limpio del stack WiFi: evita que el modo STA anterior deje
    // el DHCP del AP sin arrancar (movil: "no se ha obtenido una IP valida").
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.setSleep(false);

    // Portal simple: sin DNS cautivo. Acceso manual a la IP en pantalla.
    const IPAddress requestedApIp(192, 168, 4, 1);
    const IPAddress gatewayIp(192, 168, 4, 1);
    const IPAddress subnetMask(255, 255, 255, 0);

    bool apConfigOk = WiFi.softAPConfig(requestedApIp, gatewayIp, subnetMask);
    if (!apConfigOk) {
        delay(200);
        apConfigOk = WiFi.softAPConfig(requestedApIp, gatewayIp, subnetMask);
    }

    uint64_t chipId = ESP.getEfuseMac();
    char suffix[7] = {};
    snprintf(suffix, sizeof(suffix), "%06X", static_cast<unsigned>(chipId & 0xFFFFFFULL));
    m_apSsidText = String(AppConfig::kSetupApSsidPrefix) + suffix;

    const bool apStarted = WiFi.softAP(
        m_apSsidText.c_str(),
        AppConfig::kSetupApPassword,
        AppConfig::kSetupApChannel,
        false,
        AppConfig::kSetupApMaxClients);

    const uint32_t ipWaitStartMs = millis();
    IPAddress apIp;
    do {
        apIp = WiFi.softAPIP();
        if (apIp != IPAddress(0, 0, 0, 0)) {
            break;
        }
        delay(50);
    } while ((millis() - ipWaitStartMs) < 3000);

    m_apIpText = apIp.toString();
    m_portalStatusLine1 = "Portal WiFi activo";
    m_portalStatusLine2 = "Config en /config/radar.json";

    const bool dnsStarted = m_dnsServer.start(AppConfig::kSetupDnsPort, "*", requestedApIp);
    if (!dnsStarted) {
        RADAR_LOG_PRINTLN("WARN: captive DNS did not start; use manual IP shown on screen");
    }

    if (!apStarted || !apConfigOk || m_apIpText == "0.0.0.0") {
        RADAR_LOG_PRINTF(
            "WARN: setup AP failed (started=%d config=%d ip=%s)\n",
            static_cast<int>(apStarted),
            static_cast<int>(apConfigOk),
            m_apIpText.c_str());
        m_portalStatusLine1 = "AP WiFi con error";
        m_portalStatusLine2 = "Reinicia la placa";
    } else {
        RADAR_LOG_PRINTF(
            "Setup AP ready: SSID=%s IP=%s DNS=%d\n",
            m_apSsidText.c_str(),
            m_apIpText.c_str(),
            static_cast<int>(dnsStarted));
    }

    m_webServer.on("/", handleRootThunk);
    m_webServer.on("/generate_204", handleRootThunk);
    m_webServer.on("/fwlink", handleRootThunk);
    m_webServer.on("/save", handleSaveThunk);
    m_webServer.onNotFound(handleRootThunk);
    m_webServer.begin();

    return apStarted && apConfigOk && m_apIpText != "0.0.0.0";
}

void RadarApp::stopSetupPortal()
{
    m_webServer.close();
    m_dnsServer.stop();
    WiFi.softAPdisconnect(true);

    if (m_configSavePending) {
        saveRuntimeConfigNow();
    }
    if (!m_configSavePending) {
        RuntimeConfig sdConfig = m_runtimeConfig;
        if (m_configStore.load(sdConfig)) {
            m_runtimeConfig = sdConfig;
        }
    }

    m_pomodoroFocusMinutes = m_runtimeConfig.pomodoroFocusMinutes;
    m_pomodoroBreakMinutes = m_runtimeConfig.pomodoroBreakMinutes;
    m_pomodoroLongBreakMinutes = m_runtimeConfig.pomodoroLongBreakMinutes;
    m_waterReminderMinutes = m_runtimeConfig.waterReminderMinutes;
    m_stretchReminderMinutes = m_runtimeConfig.stretchReminderMinutes;
    m_pomodoroRemainingMs = pomodoroPhaseDurationMs();
    applyRuntimeConfigToSystems();
    setBrightness(m_runtimeConfig.brightness);
}

void RadarApp::enterSetupPortal()
{
    m_resumeViewMode = m_viewMode;
    stopRadarMode();
    startSetupPortal();
    m_lastRenderMs = 0;
}

void RadarApp::exitSetupPortal()
{
    stopSetupPortal();
    m_viewMode = m_resumeViewMode;
    startRadarMode();
    m_lastRenderMs = 0;
}

bool RadarApp::startRadarMode()
{
    m_mode = Mode::Radar;
    m_fetchMutex = xSemaphoreCreateMutex();
    m_lastRenderMs = 0;
    m_clockWifiSuspended = false;
    m_hasRadarSweepAngle = false;
    m_lastRadarSweepAngleDeg = 0.0f;
    m_refreshRadarLabelsOnNextRender = true;
    m_radarLabelNorthPassesSinceRefresh = 0;
    m_lastRadarNorthSnapshotMs = 0;
    m_radarViewEnteredMs = millis();
    m_wifiDisconnectedSinceMs = 0;
    m_trafficApiFailureVisible = false;

    if (m_runtimeConfig.hasWiFi()) {
        WiFi.mode(WIFI_STA);
    }

    return true;
}

void RadarApp::stopRadarMode()
{
    m_hasPendingFetch = false;
    m_fetchInProgress = false;
    if (m_networkTaskHandle != nullptr) {
        vTaskDelete(m_networkTaskHandle);
        m_networkTaskHandle = nullptr;
    }
    if (m_fetchMutex != nullptr) {
        vSemaphoreDelete(m_fetchMutex);
        m_fetchMutex = nullptr;
    }
    WiFi.disconnect(true);
    m_clockWifiSuspended = false;
}

void RadarApp::renderCurrentMode()
{
    if (m_mode == Mode::SetupPortal) {
        m_renderer.renderSetupScreen(
            m_apSsidText.isEmpty() ? String(AppConfig::kSetupApSsid) : m_apSsidText,
            AppConfig::kSetupApPassword,
            m_apIpText,
            m_portalStatusLine1,
            m_portalStatusLine2,
            m_runtimeConfig.radarTheme);
        flushDisplay();
        return;
    }

    if (m_viewMode == ViewMode::Radar) {
        const uint32_t nowMs = millis();
        // Las etiquetas se recalculan como snapshot: al entrar al radar y cuando
        // el barrido cruza el norte. Entre medias se reutiliza el layout congelado.
        const bool refreshLabels = m_refreshRadarLabelsOnNextRender || m_lastLabelRefreshMs == 0;
        if (refreshLabels) {
            m_lastLabelRefreshMs = nowMs;
            m_refreshRadarLabelsOnNextRender = false;
        }
        const bool wifiReady = wifiStationReady();
        if (!m_runtimeConfig.hasWiFi() || wifiReady) {
            m_wifiDisconnectedSinceMs = 0;
        } else if (m_wifiDisconnectedSinceMs == 0) {
            m_wifiDisconnectedSinceMs = nowMs;
        }
        const bool radarWifiWarningGraceElapsed =
            (nowMs - m_radarViewEnteredMs) >= kRadarWifiWarningGraceMs;
        const bool wifiNoWarningVisible = m_runtimeConfig.hasWiFi()
            && !wifiReady
            && radarWifiWarningGraceElapsed
            && m_wifiDisconnectedSinceMs != 0
            && (nowMs - m_wifiDisconnectedSinceMs) >= kWifiNoWarningDelayMs;
        const bool apiNoWarningVisible =
            radarWifiWarningGraceElapsed && m_trafficApiFailureVisible;
        String radarStatus = m_radarStatusText;
        if (nowMs >= m_radarStatusUntilMs) {
            radarStatus = "";
        }
        if (!wifiReady && wifiNoWarningVisible && m_wifiConnectStatus[0] != '\0') {
            radarStatus = m_wifiConnectStatus;
        }
        m_renderer.render(m_model, wifiReady,
            m_runtimeConfig.hasWiFi(), m_fetchInProgress,
            wifiNoWarningVisible, apiNoWarningVisible,
            m_runtimeConfig.locationLabel, radarStatus,
            m_runtimeConfig.radarTheme, refreshLabels, m_radarLabelMode,
            m_runtimeConfig.militaryOnlyEnabled);
        if (m_lastDeselectedPlaneMarkerUntilMs != 0 && nowMs >= m_lastDeselectedPlaneMarkerUntilMs) {
            m_lastDeselectedPlaneId = "";
            m_lastDeselectedPlaneMarkerUntilMs = 0;
        }
        if (!m_planePopupOpen && m_lastDeselectedPlaneMarkerUntilMs != 0) {
            m_renderer.renderPlaneCornerMarker(m_model, m_lastDeselectedPlaneId,
                m_runtimeConfig.radarTheme, nowMs, m_lastDeselectedPlaneMarkerUntilMs);
        }
        if (m_planePopupOpen && m_runtimeConfig.radarTheme != AppConfig::kRadarThemeDragonBall) {
            m_renderer.renderPlanePopup(m_planePopupPlane, m_runtimeConfig.radarTheme,
                m_runtimeConfig.militaryOnlyEnabled);
        }
    } else if (m_viewMode == ViewMode::Clock) {
        String timeText = "--:--";
        String secondsText = "--";
        String dateText = "SIN FECHA";
        String dayText = "---";
        String zuluText = "-- -- -- z";
        const time_t now = time(nullptr);
        tm localInfo{};
        tm utcInfo{};
        if (now > 100000
            && localtime_r(&now, &localInfo) != nullptr
            && gmtime_r(&now, &utcInfo) != nullptr) {
            char timeBuffer[6];
            char secondBuffer[3];
            char dateBuffer[16];
            char zuluBuffer[12];
            strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &localInfo);
            strftime(secondBuffer, sizeof(secondBuffer), "%S", &localInfo);
            strftime(zuluBuffer, sizeof(zuluBuffer), "%H %M %S z", &utcInfo);
            timeText = timeBuffer;
            secondsText = secondBuffer;
            zuluText = zuluBuffer;

            static const char* days[] = {"DOMINGO","LUNES","MARTES","MIERCOLES","JUEVES","VIERNES","SABADO"};
            static const char* months[] = {"ENE","FEB","MAR","ABR","MAY","JUN","JUL","AGO","SEP","OCT","NOV","DIC"};
            snprintf(dateBuffer, sizeof(dateBuffer), "%d %s %d",
                localInfo.tm_mday, months[localInfo.tm_mon], localInfo.tm_year % 100);
            dateText = dateBuffer;
            dayText = days[localInfo.tm_wday];
        }
        m_renderer.renderClockScreen(timeText, secondsText, dateText, dayText,
            zuluText, wifiStationReady(), m_alarmRinging,
            m_runtimeConfig.radarTheme,
            m_imu.available() ? m_imu.tiltX() : 0.0f,
            m_imu.available() ? m_imu.tiltY() : 0.0f,
            m_imu.available() ? m_imu.rotationRad() : 0.0f);
    } else if (m_viewMode == ViewMode::Sky) {
        const time_t now = time(nullptr);
        const bool skyFrameChanged = m_renderer.renderSkyScreen(now,
            m_runtimeConfig.ownshipLat,
            m_runtimeConfig.ownshipLon,
            m_runtimeConfig.rangeKm,
            systemTimeValid(),
            m_runtimeConfig.radarTheme,
            m_skyLabelMode);

        // Si el renderer detecta que el cielo no ha cambiado, no enviamos el
        // mismo framebuffer otra vez a la pantalla. La imagen actual se mantiene.
        if (!skyFrameChanged) {
            return;
        }
    } else if (m_viewMode == ViewMode::Watch) {
        uint8_t hour = 0;
        uint8_t minute = 0;
        uint8_t second = 0;
        float secondFraction = 0.0f;
        bool timeValid = false;
        // gettimeofday nos da el mismo segundo que time() m�s los microsegundos
        // dentro del segundo, que usamos para que el arco avance suave entre ticks.
        timeval tv{};
        gettimeofday(&tv, nullptr);
        const time_t now = tv.tv_sec;
        tm localInfo{};
        if (now > 100000 && localtime_r(&now, &localInfo) != nullptr) {
            hour = static_cast<uint8_t>(localInfo.tm_hour);
            minute = static_cast<uint8_t>(localInfo.tm_min);
            second = static_cast<uint8_t>(localInfo.tm_sec);
            secondFraction = std::clamp(static_cast<float>(tv.tv_usec) / 1000000.0f, 0.0f, 0.999f);
            timeValid = true;
        }
        m_renderer.renderWatchScreen(hour, minute, second, secondFraction, timeValid,
            m_runtimeConfig.alarmEnabled, m_alarmRinging,
            m_runtimeConfig.alarmHour, m_runtimeConfig.alarmMinute,
            m_runtimeConfig.radarTheme);
    } else if (m_viewMode == ViewMode::Options) {
        m_renderer.renderOptionsScreen(
            m_runtimeConfig.rangeKm,
            m_runtimeConfig.radarTheme,
            m_runtimeConfig.alarmEnabled,
            m_runtimeConfig.alarmHour,
            m_runtimeConfig.alarmMinute,
            m_runtimeConfig.northBeepEnabled,
            m_runtimeConfig.brightness,
            m_runtimeConfig.apiRefreshSeconds,
            m_runtimeConfig.fps,
            m_runtimeConfig.gyroFps,
            m_runtimeConfig.idleDimEnabled,
            m_runtimeConfig.militaryOnlyEnabled);
    } else if (m_viewMode == ViewMode::AlternativeClock) {
        String timeText = "--:--";
        int currentMinutesOfDay = -1;
        uint8_t weekdayIndex = 255;
        const time_t now = time(nullptr);
        tm localInfo{};
        if (now > 100000 && localtime_r(&now, &localInfo) != nullptr) {
            char timeBuffer[6];
            strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &localInfo);
            timeText = timeBuffer;
            currentMinutesOfDay = (localInfo.tm_hour * 60) + localInfo.tm_min;
            weekdayIndex = static_cast<uint8_t>(localInfo.tm_wday);
        }
        m_renderer.renderAlternativeClockScreen(timeText, currentMinutesOfDay,
            m_runtimeConfig.alarmEnabled, m_alarmRinging,
                m_runtimeConfig.alarmHour, m_runtimeConfig.alarmMinute, weekdayIndex);
    } else if (m_viewMode == ViewMode::DayClock) {
        String dayText = "TODAY";
        String dateText = "-- ---";
        String timeText = "--:--";
        String weatherTempText = "--C";
        String weatherConditionText = m_runtimeConfig.hasWiFi() ? String("WAIT") : String("NO WIFI");
        uint8_t minute = 0;
        bool timeValid = false;
        const bool weatherValid = m_dayClockWeather.ok;
        if (weatherValid) {
            weatherTempText = String(static_cast<int>(lroundf(m_dayClockWeather.temperatureC))) + "C";
            weatherConditionText = m_dayClockWeather.conditionText;
        } else if (m_fetchInProgress && m_dayClockWeatherRequested) {
            weatherConditionText = "WAIT";
        } else if (m_dayClockWeather.statusText.indexOf("WiFi") >= 0
                   || m_dayClockWeather.statusText.indexOf("wifi") >= 0) {
            weatherConditionText = "NO WIFI";
        } else if (!m_dayClockWeather.statusText.isEmpty()) {
            weatherConditionText = "NO DATA";
        }
        const time_t now = time(nullptr);
        tm localInfo{};
        if (now > 100000 && localtime_r(&now, &localInfo) != nullptr) {
            static const char* days[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
            static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
            char dateBuffer[8];
            char timeBuffer[6];
            snprintf(dateBuffer, sizeof(dateBuffer), "%02d %s", localInfo.tm_mday, months[localInfo.tm_mon]);
            strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &localInfo);
            dayText = days[localInfo.tm_wday];
            dateText = dateBuffer;
            timeText = timeBuffer;
            minute = static_cast<uint8_t>(localInfo.tm_min);
            timeValid = true;
        }
        m_lastDayClockMinuteKey = timeValid ? static_cast<int>(minute) : -1;
        m_renderer.renderDayClockScreen(dayText, dateText, timeText,
            weatherTempText, weatherConditionText, minute,
            timeValid, weatherValid, m_runtimeConfig.alarmEnabled,
            m_runtimeConfig.alarmHour, m_runtimeConfig.alarmMinute,
            m_runtimeConfig.radarTheme);
    } else if (m_viewMode == ViewMode::Image) {
        ImageSource::Mood faceMood = ImageSource::Mood::Neutral;
        String faceStatus = "NORMAL";
        bool faceLongBreak = false;
        if (m_waterReminderVisible) {
            faceMood = ImageSource::Mood::Hydrate;
            faceStatus = "AGUA";
        } else if (m_stretchReminderVisible) {
            faceMood = ImageSource::Mood::Stretch;
            faceStatus = "OJOS / DESCANSO";
        } else if (m_pomodoroStarted) {
            if (m_pomodoroBreakPhase) {
                faceLongBreak = m_pomodoroCompletedFocus > 0 && (m_pomodoroCompletedFocus % 4) == 0;
                faceMood = ImageSource::Mood::Break;
                faceStatus = faceLongBreak ? "DESCANSO LARGO" : "DESCANSO CORTO";
            } else if (m_pomodoroRunning) {
                faceMood = ImageSource::Mood::Focus;
                faceStatus = "FOCUS";
            } else if (m_pomodoroFocusReady) {
                faceMood = ImageSource::Mood::Paused;
                faceStatus = "FOCUS LISTO - TOCA START";
            } else {
                faceMood = ImageSource::Mood::Paused;
                faceStatus = "FOCUS PAUSADO";
            }
        } else if (m_idleSleepActive) {
            faceMood = ImageSource::Mood::Sleep;
            faceStatus = "DORMIR";
        }
        if (m_pomodoroStarted) {
            uint8_t cycleIndex = static_cast<uint8_t>((m_pomodoroCompletedFocus % 4) + 1);
            if (m_pomodoroBreakPhase && m_pomodoroCompletedFocus > 0) {
                cycleIndex = static_cast<uint8_t>(((m_pomodoroCompletedFocus - 1) % 4) + 1);
            }
            faceStatus += " " + String(cycleIndex) + " DE 4";
        }
        ImageSource::setMood(faceMood);
        ImageFrame565View frame = ImageSource::currentImageFrame(millis());
        frame.detail = (faceStatus == "NORMAL") ? nullptr : faceStatus.c_str();
        const uint16_t frameScale = frame.scale > 0 ? frame.scale : 1;
        const bool fullScreenImage = (static_cast<uint32_t>(frame.width) * frameScale) == AppConfig::kScreenWidth
            && (static_cast<uint32_t>(frame.height) * frameScale) == AppConfig::kScreenHeight;
        const bool fullScreenCanvas = frame.canvasWidth == AppConfig::kScreenWidth
            && frame.canvasHeight == AppConfig::kScreenHeight;
        frame.screenOffsetY = (frame.detail == nullptr && !fullScreenImage && !fullScreenCanvas) ? 10 : 0;
        String faceTime;
        if (frame.detail != nullptr) {
            const time_t now = time(nullptr);
            tm localInfo{};
            char timeBuffer[6];
            if (now > 100000 && localtime_r(&now, &localInfo) != nullptr) {
                strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &localInfo);
                faceTime = timeBuffer;
                frame.timeLabel = faceTime.c_str();
            }
        }
        if (m_pomodoroStarted) {
            const uint32_t durationMs = pomodoroPhaseDurationMs();
            if (durationMs > 0) {
                const uint32_t elapsedMs = durationMs > m_pomodoroRemainingMs
                    ? durationMs - m_pomodoroRemainingMs
                    : 0;
                const uint32_t totalMinutes = std::max<uint32_t>(1, (durationMs + 59999UL) / 60000UL);
                const uint32_t elapsedMinutes = std::min<uint32_t>(totalMinutes, elapsedMs / 60000UL);
                frame.progressVisible = true;
                if (!m_pomodoroRunning) {
                    frame.progressStyle = ImageProgressStyle::Paused;
                } else if (m_pomodoroBreakPhase) {
                    frame.progressStyle = faceLongBreak
                        ? ImageProgressStyle::LongBreak
                        : ImageProgressStyle::ShortBreak;
                } else {
                    frame.progressStyle = ImageProgressStyle::Focus;
                }
                frame.progressPercent = static_cast<uint8_t>(
                    std::min<uint32_t>(100, (elapsedMinutes * 100UL) / totalMinutes));
            }
        }
        m_renderer.renderImageScreen(frame);
    } else if (m_viewMode == ViewMode::PomodoroControl) {
        const uint32_t durationMs = pomodoroPhaseDurationMs();
        const uint32_t elapsedMs = durationMs > m_pomodoroRemainingMs
            ? durationMs - m_pomodoroRemainingMs
            : 0;
        const uint8_t progress = durationMs > 0
            ? static_cast<uint8_t>(std::min<uint32_t>(100, (elapsedMs * 100UL) / durationMs))
            : 0;
        const String reminderState = m_waterReminderVisible
            ? String("DRINK WATER")
            : (m_stretchReminderVisible ? String("STRETCH / EYES") : pomodoroStateText());
        m_renderer.renderPomodoroScreen("POMODORO", pomodoroTimeText(),
            reminderState,
            "TAP: START/PAUSE   RIGHT: RESET", progress, m_pomodoroRunning,
            m_pomodoroBreakPhase, false, m_runtimeConfig.radarTheme);
    } else if (m_viewMode == ViewMode::PomodoroSettings) {
        String settings = reminderIntervalText(m_pomodoroFocusMinutes)
            + "|" + reminderIntervalText(m_pomodoroBreakMinutes)
            + "|" + reminderIntervalText(m_pomodoroLongBreakMinutes)
            + "|" + String(m_pomodoroSoundEnabled ? "YES" : "NO")
            + "|" + reminderIntervalText(m_waterReminderMinutes)
            + "|" + reminderIntervalText(m_stretchReminderMinutes);
        m_renderer.renderPomodoroScreen("POMODORO SET", settings, "SETTINGS",
            "tap card to adjust", 0, false, m_pomodoroBreakPhase,
            true, m_runtimeConfig.radarTheme);
    }

    flushDisplay();
}

void RadarApp::flushDisplay()
{
    if (m_lcd == nullptr) {
        RADAR_LOG_PRINTLN("ERROR: flushDisplay without LCD");
        return;
    }
    if (m_renderer.buffer() == nullptr) {
        RADAR_LOG_PRINTLN("ERROR: flushDisplay without framebuffer");
        return;
    }

    if (m_renderer.preferPartialFlush() && m_renderer.dirtyRegionCount() > 0) {
        if (m_partialUploadBuffer == nullptr) {
            m_partialUploadBuffer = static_cast<lv_color_t*>(
                heap_caps_malloc(AppConfig::kPartialFlushMaxPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (m_partialUploadBuffer == nullptr) {
                RADAR_LOG_PRINTLN("WARN: partial upload buffer allocation failed, falling back to full flush");
            }
        }

        if (m_partialUploadBuffer != nullptr) {
            bool partialFlushOk = true;
            const lv_color_t* src = m_renderer.buffer();
            for (size_t i = 0; i < m_renderer.dirtyRegionCount(); ++i) {
                int x = 0;
                int y = 0;
                int w = 0;
                int h = 0;
                if (!m_renderer.dirtyRegionAt(i, x, y, w, h) || w <= 0 || h <= 0) {
                    continue;
                }

                const size_t rectPixels = static_cast<size_t>(w) * static_cast<size_t>(h);
                if (w != AppConfig::kScreenWidth && rectPixels > AppConfig::kPartialFlushMaxPixels) {
                    RADAR_LOG_PRINTLN("WARN: dirty rect too large for partial buffer; falling back to full flush");
                    partialFlushOk = false;
                    break;
                }

                if (w == AppConfig::kScreenWidth) {
                    const lv_color_t* rectPtr = src + (static_cast<size_t>(y) * static_cast<size_t>(AppConfig::kScreenWidth));
                    m_lcd->drawBitmap(x, y, w, h, reinterpret_cast<const uint8_t*>(rectPtr));
                    continue;
                }

                for (int row = 0; row < h; ++row) {
                    const lv_color_t* srcRow = src + (static_cast<size_t>(y + row) * static_cast<size_t>(AppConfig::kScreenWidth)) + static_cast<size_t>(x);
                    lv_color_t* dstRow = m_partialUploadBuffer + (static_cast<size_t>(row) * static_cast<size_t>(w));
                    std::memcpy(dstRow, srcRow, static_cast<size_t>(w) * sizeof(lv_color_t));
                }
                m_lcd->drawBitmap(x, y, w, h, reinterpret_cast<const uint8_t*>(m_partialUploadBuffer));
            }
            if (partialFlushOk) {
                return;
            }
        }
    }

    if (m_usePanelFrameBuffers) {
        lv_color_t* currentBuffer = m_lcdBuffer[m_renderBufferIndex];
        if ((currentBuffer != nullptr) && m_lcd->switchFrameBufferTo(currentBuffer)) {
            const int nextIndex = (m_renderBufferIndex + 1) % 2;
            if (m_lcdBuffer[nextIndex] != nullptr) {
                // Solo ocurre en full flush. Mantiene el siguiente backbuffer sincronizado
                // para que los dirty-rects posteriores no partan de una imagen vieja.
                const size_t fbBytes = static_cast<size_t>(AppConfig::kScreenWidth) * static_cast<size_t>(AppConfig::kScreenHeight) * sizeof(lv_color_t);
                std::memcpy(m_lcdBuffer[nextIndex], currentBuffer, fbBytes);
            }
            m_renderBufferIndex = nextIndex;
            m_renderer.attachBuffer(m_lcdBuffer[m_renderBufferIndex], AppConfig::kScreenWidth, AppConfig::kScreenHeight);
            return;
        }
        RADAR_LOG_PRINTLN("WARN: switchFrameBufferTo failed, falling back to drawBitmap");
    }

    m_lcd->drawBitmap(0, 0, AppConfig::kScreenWidth, AppConfig::kScreenHeight,
        reinterpret_cast<const uint8_t*>(m_renderer.buffer()));
}

void RadarApp::handleTouch()
{
    static constexpr int kSwipeThreshold = 38;
    static constexpr int kTapMoveTolerance = 25;
    static constexpr int kPlaneTapMoveTolerance = 40;
    static constexpr uint32_t kPlaneTapDecisionMs = 55;
    static constexpr uint32_t kPlanePopupTouchIgnoreMs = 280;
    // Antirrebote del cierre del detalle: exige que la pulsación se haya mantenido
    // un mínimo de tiempo. Filtra los "toques fantasma" (1-2 muestras sueltas que
    // generan estos controladores capacitivos, sobre todo durante el volcado de
    // pantalla completa) que antes podían cerrar la ficha sin que el usuario tocase.
    static constexpr uint32_t kPlanePopupCloseMinPressMs = 45;
    // El detalle del avión ya no se cierra con cualquier toque: solo con la franja
    // inferior grande donde aparece "TOCA PARA CERRAR". Así evitamos falsos cierres
    // al tocar la ficha, el avión central o los datos.
    static constexpr int kPlanePopupCloseX0 = 54;
    static constexpr int kPlanePopupCloseY0 = 410;
    static constexpr int kPlanePopupCloseX1 = 426;
    static constexpr int kPlanePopupCloseY1 = 470;
    static constexpr uint32_t kActionTapMinPressMs = 30;

    auto isPlanePopupCloseZone = [](int px, int py) -> bool {
        return px >= kPlanePopupCloseX0 && px <= kPlanePopupCloseX1
            && py >= kPlanePopupCloseY0 && py <= kPlanePopupCloseY1;
    };

    if (m_touch == nullptr) return;

    const uint32_t nowMs = millis();
    const bool touchInterruptPending = g_touchInterruptPending;
    const bool touchLineActive = digitalRead(kTouchInterruptPin) == kTouchInterruptActiveLevel;
    const bool forceTouchPoll = (m_mode == Mode::Radar
            && (m_viewMode == ViewMode::Options
                || m_viewMode == ViewMode::Image
                || m_viewMode == ViewMode::PomodoroControl
                || m_viewMode == ViewMode::PomodoroSettings))
        || m_mode == Mode::SetupPortal
        || m_planePopupOpen;
    if (!forceTouchPoll
        && !m_touchWasPressed
        && !touchInterruptPending
        && !touchLineActive) {
        return;
    }
    g_touchInterruptPending = false;

    // Read touch points first to clear controller buffer and get current state
    esp_panel::drivers::TouchPoint point{};
    int points = m_touch->readPoints(&point, 1, 0);
    bool pressed = points > 0;
    if (pressed && !m_touchWasPressed && !touchInterruptPending && !touchLineActive) {
        pressed = false;
    }
    const int x = point.x;
    const int y = point.y;

    if (m_configSaveModalActive) {
        m_planePopupTapPending = false;
        m_touchConsumed = true;
        m_touchWasPressed = pressed;
        return;
    }

    if (nowMs < m_touchIgnoreUntilMs) {
        m_planePopupTapPending = false;
        m_touchConsumed = true;
        m_touchWasPressed = pressed;
        return;
    }

    // --- Plane popup: ignore touches for first 500ms ---
    if (m_planePopupOpen && (nowMs - m_planePopupOpenedMs) < kPlanePopupTouchIgnoreMs) {
        m_planePopupTapPending = false;
        m_touchConsumed = true;
        m_touchWasPressed = pressed;
        return;
    }

    const bool dragonBallRadarTouch = m_mode == Mode::Radar
        && m_viewMode == ViewMode::Radar
        && m_runtimeConfig.radarTheme == AppConfig::kRadarThemeDragonBall;

    if (pressed && !m_touchWasPressed) {
        m_touchPressMs = nowMs;
        m_touchX = x;
        m_touchY = y;
        m_touchLastX = x;
        m_touchLastY = y;
        m_touchConsumed = false;
        m_planePopupTapPending = false;

        if (restoreBrightnessAfterIdle(nowMs)) {
            m_touchConsumed = true;
            m_touchWasPressed = true;
            return;
        }

        registerUserActivity(nowMs);
        if (m_waterReminderVisible || m_stretchReminderVisible) {
            m_waterReminderVisible = false;
            m_waterReminderUntilMs = 0;
            m_stretchReminderVisible = false;
            m_stretchReminderUntilMs = 0;
            m_lastRenderMs = 0;
        }

        // Si el toque empieza encima de un avion, lo marcamos como candidato a
        // popup. No abrimos al instante absoluto para no romper el gesto de swipe:
        // se confirmara unos milisegundos despues si el dedo no se ha movido.
        if (m_mode == Mode::Radar
            && m_viewMode == ViewMode::Radar
            && !m_planePopupOpen
            && !dragonBallRadarTouch) {
            const int planeIndex = m_renderer.hitTestPlaneIndex(m_model, x, y, m_runtimeConfig.radarTheme);
            m_planePopupTapPending = planeIndex >= 0;
        }

        if (m_mode == Mode::Radar && m_viewMode == ViewMode::Options) {
            m_touchWasPressed = true;
            return;
        }
    }

    if (pressed) {
        m_touchLastX = x;
        m_touchLastY = y;
    }

    if (pressed && m_planePopupTapPending) {
        const int dx = m_touchLastX - m_touchX;
        const int dy = m_touchLastY - m_touchY;
        if (abs(dx) > kPlaneTapMoveTolerance || abs(dy) > kPlaneTapMoveTolerance) {
            m_planePopupTapPending = false;
        }
    }

    if (pressed && !m_touchConsumed && m_mode == Mode::Radar && !m_planePopupOpen) {
        const int dx = m_touchLastX - m_touchX;
        const int dy = m_touchLastY - m_touchY;
        if (abs(dx) > kSwipeThreshold && abs(dx) > abs(dy)) {
            changeColumnStep(dx > 0 ? 1 : -1);
            m_touchConsumed = true;
            m_lastRenderMs = 0;
            m_touchWasPressed = true;
            return;
        }
        if (abs(dy) > kSwipeThreshold && abs(dy) > abs(dx)) {
            changeColumnRowStep(dy < 0 ? -1 : 1);
            m_touchConsumed = true;
            m_lastRenderMs = 0;
            m_touchWasPressed = true;
            return;
        }
    }

    // Apertura rapida del detalle de avion: evita esperar al release, pero deja
    // una microventana para distinguir tap de swipe.
    if (pressed
        && !m_touchConsumed
        && m_planePopupTapPending
        && !m_planePopupOpen
        && (nowMs - m_touchPressMs) >= kPlaneTapDecisionMs) {
        const int dx = m_touchLastX - m_touchX;
        const int dy = m_touchLastY - m_touchY;
        if (abs(dx) <= kPlaneTapMoveTolerance && abs(dy) <= kPlaneTapMoveTolerance) {
            if (tryOpenPlanePopup(m_touchX, m_touchY)
                || tryOpenPlanePopup(m_touchLastX, m_touchLastY)
                || tryOpenPlanePopup((m_touchX + m_touchLastX) / 2,
                                     (m_touchY + m_touchLastY) / 2)) {
                m_planePopupTapPending = false;
                m_touchConsumed = true;
                m_lastRenderMs = 0;
                m_touchWasPressed = true;
                return;
            }
        } else {
            m_planePopupTapPending = false;
        }
    }

    if (pressed && !m_touchConsumed && !dragonBallRadarTouch && !m_planePopupOpen) {
        const uint32_t elapsed = nowMs - m_touchPressMs;
        if (elapsed >= AppConfig::kTouchLongPressMs) {
            const int dx = m_touchLastX - m_touchX;
            const int dy = m_touchLastY - m_touchY;
            if (abs(dx) < kTapMoveTolerance && abs(dy) < kTapMoveTolerance) {
                m_touchConsumed = true;
                if (m_mode == Mode::Radar) {
                    enterSetupPortal();
                    m_touchWasPressed = true;
                    return;
                } else if (m_mode == Mode::SetupPortal) {
                    exitSetupPortal();
                    m_touchWasPressed = true;
                    return;
                }
            }
        }
    }

    if (!pressed && m_touchWasPressed && !m_touchConsumed) {
        m_planePopupTapPending = false;
        const int dx = m_touchLastX - m_touchX;
        const int dy = m_touchLastY - m_touchY;
        const bool actionTapHeldLongEnough = (nowMs - m_touchPressMs) >= kActionTapMinPressMs;

        if (m_planePopupOpen) {
            const bool shortTap = abs(dx) < kTapMoveTolerance && abs(dy) < kTapMoveTolerance;
            const bool closeZoneTap = isPlanePopupCloseZone(m_touchX, m_touchY)
                && isPlanePopupCloseZone(m_touchLastX, m_touchLastY);

            const bool heldLongEnough = (nowMs - m_touchPressMs) >= kPlanePopupCloseMinPressMs;
            if ((nowMs - m_planePopupOpenedMs) >= kPlanePopupTouchIgnoreMs
                && heldLongEnough
                && shortTap
                && closeZoneTap) {
                registerUserActivity(nowMs);
                closePlanePopup();
                m_renderer.forceNextRadarFullFrame();
                m_lastRenderMs = 0;
            }
            m_touchConsumed = true;
            m_touchWasPressed = pressed;
            return;
        }

        if (m_alarmRinging
                   && abs(dx) < kTapMoveTolerance && abs(dy) < kTapMoveTolerance) {
            dismissClockAlarm();
            m_lastRenderMs = 0;
        } else if (m_viewMode == ViewMode::Options) {
            const bool shortTap = abs(dx) < kTapMoveTolerance && abs(dy) < kTapMoveTolerance;
            if (abs(dx) > kSwipeThreshold && abs(dx) > abs(dy)) {
                changeColumnStep(dx > 0 ? 1 : -1);
                m_lastRenderMs = 0;
            } else if (shortTap && findOptionsTouchBox(m_touchX, m_touchY) != nullptr) {
                handleOptionsShortTap(m_touchX, m_touchY);
                m_lastRenderMs = 0;
            } else if (shortTap && findOptionsTouchBox(m_touchLastX, m_touchLastY) != nullptr) {
                handleOptionsShortTap(m_touchLastX, m_touchLastY);
                m_lastRenderMs = 0;
            }
        } else if (m_viewMode == ViewMode::PomodoroControl) {
            const bool shortTap = abs(dx) < kTapMoveTolerance && abs(dy) < kTapMoveTolerance;
            if (abs(dx) > kSwipeThreshold && abs(dx) > abs(dy)) {
                changeColumnStep(dx > 0 ? 1 : -1);
                m_lastRenderMs = 0;
            } else if (abs(dy) > kSwipeThreshold && abs(dy) > abs(dx)) {
                changeColumnRowStep(dy < 0 ? -1 : 1);
                m_lastRenderMs = 0;
            } else if (shortTap && actionTapHeldLongEnough) {
                handlePomodoroControlTap(m_touchLastX, m_touchLastY);
                m_lastRenderMs = 0;
            }
        } else if (m_viewMode == ViewMode::PomodoroSettings) {
            const bool shortTap = abs(dx) < kTapMoveTolerance && abs(dy) < kTapMoveTolerance;
            const bool sameCard = (m_touchX < (AppConfig::kScreenWidth / 2))
                    == (m_touchLastX < (AppConfig::kScreenWidth / 2))
                && (m_touchY < (AppConfig::kScreenHeight / 2))
                    == (m_touchLastY < (AppConfig::kScreenHeight / 2));
            if (abs(dx) > kSwipeThreshold && abs(dx) > abs(dy)) {
                changeColumnStep(dx > 0 ? 1 : -1);
                m_lastRenderMs = 0;
            } else if (abs(dy) > kSwipeThreshold && abs(dy) > abs(dx)) {
                changeColumnRowStep(dy < 0 ? -1 : 1);
                m_lastRenderMs = 0;
            } else if (shortTap && sameCard && actionTapHeldLongEnough) {
                handlePomodoroSettingsTap((m_touchX + m_touchLastX) / 2,
                    (m_touchY + m_touchLastY) / 2);
                m_lastRenderMs = 0;
            }
        } else if (abs(dx) > kSwipeThreshold && abs(dx) > abs(dy)) {
            changeColumnStep(dx > 0 ? 1 : -1);
            m_lastRenderMs = 0;
        } else if (abs(dy) > kSwipeThreshold && abs(dy) > abs(dx)) {
            changeColumnRowStep(dy < 0 ? -1 : 1);
            m_lastRenderMs = 0;
        } else if (dragonBallRadarTouch
                   && abs(dx) < kTapMoveTolerance && abs(dy) < kTapMoveTolerance) {
            if (m_renderer.startDragonBallScan(nowMs)) {
                startDragonBallTouchBeep(nowMs);
                const bool apiAllowed = !m_hasDragonBallApiFetch
                    || (nowMs - m_lastDragonBallApiFetchMs) >= AppConfig::kDragonBallApiMinIntervalMs;
                if (apiAllowed && scheduleNetworkFetch(nowMs, AppConfig::kDragonBallApiMinIntervalMs, true, false)) {
                    m_hasDragonBallApiFetch = true;
                    m_lastDragonBallApiFetchMs = nowMs;
                }
            }
            m_lastRenderMs = 0;
        } else if (m_viewMode == ViewMode::Radar) {
            // En radar, todo lo que no llega a umbral de swipe se considera tap.
            // Evita la zona muerta anterior: 25-50 px no era ni tap ni swipe.
            const bool looksLikeTap = abs(dx) <= kSwipeThreshold && abs(dy) <= kSwipeThreshold;
            if (looksLikeTap) {
                if (!tryOpenPlanePopup(m_touchX, m_touchY)
                    && !tryOpenPlanePopup(m_touchLastX, m_touchLastY)
                    && !tryOpenPlanePopup((m_touchX + m_touchLastX) / 2,
                                         (m_touchY + m_touchLastY) / 2)
                    && AppConfig::kEnableRadarLabelModeTapCycle) {
                    m_radarLabelMode = static_cast<uint8_t>((m_radarLabelMode + 1) % AppConfig::kLabelModeCount);
                }
                m_lastRenderMs = 0;
            }
        } else if (m_viewMode == ViewMode::Sky
                   && abs(dx) < kTapMoveTolerance && abs(dy) < kTapMoveTolerance) {
            m_skyLabelMode = static_cast<uint8_t>((m_skyLabelMode + 1) % 3);
            m_lastRenderMs = 0;
        } else if (m_viewMode == ViewMode::Image
                   && m_pomodoroStarted
                   && abs(dx) < kTapMoveTolerance && abs(dy) < kTapMoveTolerance
                   && actionTapHeldLongEnough) {
            togglePomodoro(nowMs);
            m_lastRenderMs = 0;
        }
        m_touchConsumed = true;
    }

    if (!pressed) {
        m_planePopupTapPending = false;
    }

    m_touchWasPressed = pressed;
}

void RadarApp::handleOptionsShortTap(int x, int y)
{
    const OptionsTouchBox* box = findOptionsTouchBox(x, y);
    if (box == nullptr) {
        return;
    }
    ImageSource::notifyInteraction(ImageSource::Interaction::OptionChanged, millis());

    // Bot�n WIFI CONFIG: entra directo al portal de configuraci�n (como el
    // long-press, pero pinchando). No modifica ninguna opci�n.
    if (box == &kOptionsWifiBox) {
        enterSetupPortal();
        return;
    }

    // Bot�n AUTO DIM: conmuta la atenuaci�n progresiva (solo RAM).
    if (box == &kOptionsMilBox) {
        m_runtimeConfig.militaryOnlyEnabled = !m_runtimeConfig.militaryOnlyEnabled;
        persistRuntimeConfig();
        scheduleNetworkFetch(millis(), 0, true, false);
        m_lastRenderMs = 0;
        return;
    }

    if (box == &kOptionsDimBox) {
        m_runtimeConfig.idleDimEnabled = !m_runtimeConfig.idleDimEnabled;
        persistRuntimeConfig();
        updateIdleBrightness(millis());
        return;
    }

    const int delta = box->toggleOnly ? 0 : optionsDeltaForPoint(x, *box);

    if (box == &kOptionsDistanceBox) {
        changeRangeStep(delta);
        return;
    }

    if (box == &kOptionsBorderBox) {
        changeRadarThemeStep(delta);
        return;
    }

    if (box == &kOptionsBrightnessBox) {
        cycleBrightnessStep(delta);
        return;
    }

    if (box == &kOptionsSweepBox) {
        changeApiRefreshStep(delta);
        return;
    }

    if (box == &kOptionsHourBox) {
        cycleAlarmHourStep(delta);
        return;
    }

    if (box == &kOptionsMinuteBox) {
        cycleAlarmMinuteStep(delta);
        return;
    }

    if (box == &kOptionsRadarFpsBox) {
        changeFpsStep(delta);
        return;
    }

    if (box == &kOptionsGyroFpsBox) {
        changeGyroFpsStep(delta);
        return;
    }

    // ON/OFF: toda la tarjeta alterna el estado, sin dividir izquierda/derecha.
    if (box == &kOptionsAlarmBox) {
        setAlarmEnabled(!m_runtimeConfig.alarmEnabled);
        return;
    }

    if (box == &kOptionsNorthBeepBox) {
        setNorthBeepEnabled(!m_runtimeConfig.northBeepEnabled);
        return;
    }
}

// Navegaci�n en rejilla por columnas. La columna del reloj tiene una tercera
// fila para el reloj alternativo; opciones conserva su imagen en la segunda fila.
void RadarApp::handlePomodoroControlTap(int x, int y)
{
    if (pointInsidePaddedRect(x, y, kPomodoroResetX, kPomodoroResetY,
            kPomodoroResetW, kPomodoroResetH, kPomodoroButtonPad)) {
        resetPomodoro();
        m_touchIgnoreUntilMs = millis() + 300UL;
        return;
    }

    if (pointInsidePaddedRect(x, y, kPomodoroStartX, kPomodoroStartY,
            kPomodoroStartW, kPomodoroStartH, kPomodoroButtonPad)) {
        togglePomodoro(millis());
        m_touchIgnoreUntilMs = millis() + 300UL;
    }
}

void RadarApp::handlePomodoroSettingsTap(int x, int y)
{
    const uint32_t nowMs = millis();
    if (pointInsidePaddedRect(x, y, kPomodoroFocusX, kPomodoroFocusY,
            kPomodoroCardW, kPomodoroCardH, kPomodoroCardPad)) {
        cyclePomodoroFocus();
    } else if (pointInsidePaddedRect(x, y, kPomodoroBreakX, kPomodoroBreakY,
            kPomodoroCardW, kPomodoroCardH, kPomodoroCardPad)) {
        cyclePomodoroBreak();
    } else if (pointInsidePaddedRect(x, y, kPomodoroLongBreakX, kPomodoroLongBreakY,
            kPomodoroCardW, kPomodoroCardH, kPomodoroCardPad)) {
        cyclePomodoroLongBreak();
    } else if (pointInsidePaddedRect(x, y, kPomodoroSoundX, kPomodoroSoundY,
            kPomodoroCardW, kPomodoroCardH, kPomodoroCardPad)) {
        cyclePomodoroSound();
    } else if (pointInsidePaddedRect(x, y, kPomodoroWaterX, kPomodoroWaterY,
            kPomodoroCardW, kPomodoroCardH, kPomodoroCardPad)) {
        cycleWaterReminder();
    } else if (pointInsidePaddedRect(x, y, kPomodoroEyesX, kPomodoroEyesY,
            kPomodoroCardW, kPomodoroCardH, kPomodoroCardPad)) {
        cycleStretchReminder();
    } else {
        return;
    }
    m_touchIgnoreUntilMs = nowMs + 350UL;
}

void RadarApp::startOrResumePomodoro(uint32_t nowMs)
{
    if (!m_pomodoroStarted) {
        m_pomodoroStarted = true;
        m_pomodoroBreakPhase = false;
        m_pomodoroRemainingMs = pomodoroPhaseDurationMs();
    }
    m_pomodoroFocusReady = false;
    m_pomodoroRunning = true;
    m_pomodoroLastTickMs = nowMs;
    m_lastPomodoroUpdateMs = nowMs;
    ImageSource::setMood(m_pomodoroBreakPhase ? ImageSource::Mood::Break : ImageSource::Mood::Focus);
    playBuzzerCue(BuzzerCue::PomodoroStart, nowMs);
    showFaceView();
}

void RadarApp::pausePomodoro(uint32_t nowMs)
{
    if (!m_pomodoroStarted || !m_pomodoroRunning) {
        return;
    }

    if (m_pomodoroLastTickMs != 0) {
        const uint32_t elapsedMs = nowMs - m_pomodoroLastTickMs;
        if (elapsedMs >= m_pomodoroRemainingMs) {
            updatePomodoro(nowMs);
            return;
        }
        m_pomodoroRemainingMs -= elapsedMs;
    }

    const bool focusPause = m_pomodoroStarted && m_pomodoroRunning && !m_pomodoroBreakPhase;
    m_lastPomodoroUpdateMs = nowMs;
    m_pomodoroRunning = false;
    m_pomodoroFocusReady = false;
    m_pomodoroLastTickMs = nowMs;
    if (focusPause) {
        ImageSource::playMoment(ImageSource::Moment::PomodoroPaused, nowMs);
        showFaceView();
    }
    playBuzzerCue(BuzzerCue::PomodoroPause, nowMs);
}

void RadarApp::togglePomodoro(uint32_t nowMs)
{
    if (m_pomodoroRunning) {
        pausePomodoro(nowMs);
    } else {
        startOrResumePomodoro(nowMs);
    }
}

void RadarApp::resetPomodoro()
{
    const uint32_t nowMs = millis();
    m_pomodoroStarted = false;
    m_pomodoroRunning = false;
    m_pomodoroBreakPhase = false;
    m_pomodoroFocusReady = false;
    m_pomodoroCompletedFocus = 0;
    m_pomodoroRemainingMs = pomodoroPhaseDurationMs();
    m_pomodoroLastTickMs = 0;
    m_lastPomodoroUpdateMs = nowMs;
    ImageSource::playMoment(ImageSource::Moment::PomodoroReset, nowMs);
    playBuzzerCue(BuzzerCue::PomodoroReset, nowMs);
    showFaceView();
}

void RadarApp::updatePomodoro(uint32_t nowMs)
{
    auto updateReminder = [this, nowMs](uint8_t minutes, uint32_t& nextMs,
                              bool& visible, uint32_t& untilMs) -> bool {
        if (minutes == 0) {
            nextMs = 0;
            if (visible) {
                visible = false;
                untilMs = 0;
                m_lastRenderMs = 0;
            }
            return false;
        }

        bool triggered = false;
        const uint32_t intervalMs = static_cast<uint32_t>(minutes) * 60UL * 1000UL;
        if (nextMs == 0) {
            nextMs = nowMs + intervalMs;
        } else if (static_cast<int32_t>(nowMs - nextMs) >= 0) {
            visible = true;
            untilMs = nowMs + kReminderVisibleMs;
            nextMs = nowMs + intervalMs;
            triggered = true;
            m_lastRenderMs = 0;
        }
        if (visible && static_cast<int32_t>(nowMs - untilMs) >= 0) {
            visible = false;
            untilMs = 0;
            m_lastRenderMs = 0;
        }
        return triggered;
    };

    const bool waterTriggered = updateReminder(m_waterReminderMinutes, m_nextWaterReminderMs,
        m_waterReminderVisible, m_waterReminderUntilMs);
    const bool stretchTriggered = updateReminder(m_stretchReminderMinutes, m_nextStretchReminderMs,
        m_stretchReminderVisible, m_stretchReminderUntilMs);
    if (waterTriggered) {
        registerUserActivity(nowMs);
        ImageSource::setMood(ImageSource::Mood::Hydrate);
        playBuzzerCue(BuzzerCue::WaterReminder, nowMs);
        showFaceView();
    } else if (stretchTriggered) {
        registerUserActivity(nowMs);
        ImageSource::setMood(ImageSource::Mood::Stretch);
        playBuzzerCue(BuzzerCue::EyesReminder, nowMs);
        showFaceView();
    }

    if (!m_pomodoroStarted || !m_pomodoroRunning) {
        return;
    }
    if (m_pomodoroLastTickMs == 0) {
        m_pomodoroLastTickMs = nowMs;
        return;
    }

    uint32_t elapsedMs = nowMs - m_pomodoroLastTickMs;
    m_pomodoroLastTickMs = nowMs;
    bool completedFocus = false;
    bool completedBreak = false;
    while (m_pomodoroRemainingMs > 0 && elapsedMs >= m_pomodoroRemainingMs) {
        elapsedMs -= m_pomodoroRemainingMs;
        if (m_pomodoroBreakPhase) {
            m_pomodoroBreakPhase = false;
            completedBreak = true;
            m_pomodoroFocusReady = true;
            m_pomodoroRunning = false;
            m_pomodoroLastTickMs = nowMs;
            elapsedMs = 0;
        } else {
            m_pomodoroBreakPhase = true;
            m_pomodoroFocusReady = false;
            completedFocus = true;
            if (m_pomodoroCompletedFocus < 255) {
                ++m_pomodoroCompletedFocus;
            }
        }
        m_pomodoroRemainingMs = pomodoroPhaseDurationMs();
        if (completedBreak) {
            break;
        }
    }
    if (completedFocus) {
        ImageSource::setMood(m_pomodoroBreakPhase ? ImageSource::Mood::Break : ImageSource::Mood::Focus);
        ImageSource::playMoment(ImageSource::Moment::PomodoroCompleted, nowMs);
        playBuzzerCue(BuzzerCue::FocusComplete, nowMs);
        showFaceView();
    } else if (completedBreak) {
        ImageSource::setMood(ImageSource::Mood::Paused);
        playBuzzerCue(BuzzerCue::BreakComplete, nowMs);
        showFaceView();
    }
    if (elapsedMs < m_pomodoroRemainingMs) {
        m_pomodoroRemainingMs -= elapsedMs;
    }
}

void RadarApp::cyclePomodoroFocus()
{
    constexpr uint8_t options[] = {25, 30, 45, 50};
    uint8_t next = options[0];
    for (uint8_t i = 0; i < sizeof(options); ++i) {
        if (m_pomodoroFocusMinutes == options[i]) {
            next = options[(i + 1) % sizeof(options)];
            break;
        }
    }
    m_pomodoroFocusMinutes = next;
    if (!m_pomodoroStarted || !m_pomodoroBreakPhase) {
        m_pomodoroRemainingMs = pomodoroPhaseDurationMs();
    }
    persistRuntimeConfig();
    m_lastRenderMs = 0;
}

void RadarApp::cyclePomodoroBreak()
{
    constexpr uint8_t options[] = {5, 10, 15};
    uint8_t next = options[0];
    for (uint8_t i = 0; i < sizeof(options); ++i) {
        if (m_pomodoroBreakMinutes == options[i]) {
            next = options[(i + 1) % sizeof(options)];
            break;
        }
    }
    m_pomodoroBreakMinutes = next;
    if (!m_pomodoroStarted || m_pomodoroBreakPhase) {
        m_pomodoroRemainingMs = pomodoroPhaseDurationMs();
    }
    persistRuntimeConfig();
    m_lastRenderMs = 0;
}

void RadarApp::cyclePomodoroLongBreak()
{
    constexpr uint8_t options[] = {15, 20, 25, 30};
    uint8_t next = options[0];
    for (uint8_t i = 0; i < sizeof(options); ++i) {
        if (m_pomodoroLongBreakMinutes == options[i]) {
            next = options[(i + 1) % sizeof(options)];
            break;
        }
    }
    m_pomodoroLongBreakMinutes = next;
    if (m_pomodoroBreakPhase && m_pomodoroCompletedFocus > 0 && (m_pomodoroCompletedFocus % 4) == 0) {
        m_pomodoroRemainingMs = pomodoroPhaseDurationMs();
    }
    persistRuntimeConfig();
    m_lastRenderMs = 0;
}

void RadarApp::cyclePomodoroSound()
{
    m_pomodoroSoundEnabled = !m_pomodoroSoundEnabled;
    if (!m_pomodoroSoundEnabled) {
        m_buzzerCue = BuzzerCue::None;
        m_buzzerCueOn = false;
        syncBuzzerOutput();
    }
    persistRuntimeConfig();
    m_lastRenderMs = 0;
}

void RadarApp::cycleWaterReminder()
{
    constexpr uint8_t options[] = {0, 10, 15, 30, 45, 60, 90};
    uint8_t next = options[0];
    for (uint8_t i = 0; i < sizeof(options); ++i) {
        if (m_waterReminderMinutes == options[i]) {
            next = options[(i + 1) % sizeof(options)];
            break;
        }
    }
    m_waterReminderMinutes = next;
    m_nextWaterReminderMs = 0;
    m_waterReminderUntilMs = 0;
    m_waterReminderVisible = false;
    persistRuntimeConfig();
    m_lastRenderMs = 0;
}

void RadarApp::cycleStretchReminder()
{
    constexpr uint8_t options[] = {0, 10, 15, 20, 30, 45, 60};
    uint8_t next = options[0];
    for (uint8_t i = 0; i < sizeof(options); ++i) {
        if (m_stretchReminderMinutes == options[i]) {
            next = options[(i + 1) % sizeof(options)];
            break;
        }
    }
    m_stretchReminderMinutes = next;
    m_nextStretchReminderMs = 0;
    m_stretchReminderUntilMs = 0;
    m_stretchReminderVisible = false;
    persistRuntimeConfig();
    m_lastRenderMs = 0;
}

uint32_t RadarApp::pomodoroPhaseDurationMs() const
{
    uint32_t minutes = m_pomodoroBreakPhase ? m_pomodoroBreakMinutes : m_pomodoroFocusMinutes;
    if (m_pomodoroBreakPhase && m_pomodoroCompletedFocus > 0 && (m_pomodoroCompletedFocus % 4) == 0) {
        minutes = m_pomodoroLongBreakMinutes;
    }
    return minutes * 60UL * 1000UL;
}

String RadarApp::pomodoroTimeText() const
{
    const uint32_t totalSeconds = (m_pomodoroRemainingMs + 999UL) / 1000UL;
    const uint32_t minutes = totalSeconds / 60UL;
    const uint32_t seconds = totalSeconds % 60UL;
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu",
        static_cast<unsigned long>(minutes), static_cast<unsigned long>(seconds));
    return String(buffer);
}

String RadarApp::pomodoroStateText() const
{
    if (!m_pomodoroStarted) {
        return "READY";
    }
    if (m_pomodoroFocusReady) {
        return "FOCUS READY";
    }
    String text = m_pomodoroBreakPhase
        ? ((m_pomodoroCompletedFocus > 0 && (m_pomodoroCompletedFocus % 4) == 0) ? "LONG BREAK" : "SHORT BREAK")
        : "FOCUS";
    text += m_pomodoroRunning ? " RUNNING" : " PAUSED";
    return text;
}

String RadarApp::reminderIntervalText(uint8_t minutes) const
{
    if (minutes == 0) {
        return "OFF";
    }
    return String(static_cast<int>(minutes)) + "M";
}

RadarApp::ViewMode RadarApp::viewForColumnRow(int column, int row)
{
    switch (column) {
        case 0:
            return row == 0 ? ViewMode::Radar : ViewMode::Sky;
        case 1:
            if (row == 0) return ViewMode::Watch;
            if (row == 1) return ViewMode::Clock;
            if (row == 2) return ViewMode::AlternativeClock;
            return ViewMode::DayClock;
        case 2:
            return ViewMode::Options;
        case 3:
            if (row == 0) return ViewMode::PomodoroControl;
            if (row == 1) return ViewMode::Image;
            return ViewMode::PomodoroSettings;
        default: return ViewMode::Radar;
    }
}

uint8_t RadarApp::rowCountForColumn(int column)
{
    if (column == 0) return 2;
    if (column == 1) return 4;
    if (column == 3) return 3;
    return 1;
}

// Swipe horizontal: cambia de columna en bucle infinito, restaurando la �ltima
// fila usada de esa columna.
void RadarApp::changeColumnStep(int delta)
{
    const ViewMode previous = m_viewMode;
    int column = (static_cast<int>(m_currentColumn) + delta) % kColumnCount;
    if (column < 0) {
        column += kColumnCount;
    }
    m_currentColumn = static_cast<uint8_t>(column);
    const int row = findEnabledRowInColumn(m_currentColumn, m_columnRow[m_currentColumn]);
    if (row >= 0) {
        m_columnRow[m_currentColumn] = static_cast<uint8_t>(row);
        m_viewMode = viewForColumnRow(m_currentColumn, m_columnRow[m_currentColumn]);
    }
    applyViewModeChange(previous);
    if (previous != m_viewMode) {
        ImageSource::notifyInteraction(ImageSource::Interaction::ViewChanged, millis());
    }
}

// Swipe vertical: avanza por las pantallas de la columna actual y guarda la
// elecci�n para esa columna.
void RadarApp::toggleColumnRow()
{
    changeColumnRowStep(1);
}

void RadarApp::changeColumnRowStep(int delta)
{
    const ViewMode previous = m_viewMode;
    const uint8_t rowCount = rowCountForColumn(m_currentColumn);
    if (rowCount <= 1) {
        return;
    }

    if (m_currentColumn == 3) {
        int nextRow = 1; // La cara es el centro; las opciones vuelven siempre al centro.
        if (m_columnRow[m_currentColumn] == 1) {
            nextRow = delta < 0 ? 0 : 2;
        }
        if (!isViewEnabled(viewForColumnRow(m_currentColumn, nextRow))) {
            nextRow = 1;
        }
        m_columnRow[m_currentColumn] = static_cast<uint8_t>(nextRow);
        m_viewMode = viewForColumnRow(m_currentColumn, m_columnRow[m_currentColumn]);
        applyViewModeChange(previous);
        if (previous != m_viewMode) {
            ImageSource::notifyInteraction(ImageSource::Interaction::ViewChanged, millis());
            m_lastRenderMs = 0;
        }
        return;
    }

    int requestedRow = m_currentColumn == 0
        ? ((m_columnRow[m_currentColumn] + 1) % rowCount)
        : ((static_cast<int>(m_columnRow[m_currentColumn]) + delta) % rowCount);
    if (requestedRow < 0) {
        requestedRow += rowCount;
    }
    const int nextRow = m_currentColumn == 0
        ? requestedRow
        : findEnabledRowInColumn(m_currentColumn, requestedRow);

    if (nextRow < 0) {
        return;
    }

    m_columnRow[m_currentColumn] = static_cast<uint8_t>(nextRow);
    m_viewMode = viewForColumnRow(m_currentColumn, m_columnRow[m_currentColumn]);

    applyViewModeChange(previous);
    if (previous != m_viewMode) {
        ImageSource::notifyInteraction(ImageSource::Interaction::ViewChanged, millis());
        m_lastRenderMs = 0;
    }
}

// Aplica los efectos de cambiar de vista (suspensi�n/reanudaci�n de WiFi, pose del
// reloj, re-fetch al volver a Radar). Compartido por los swipes horizontal/vertical.
void RadarApp::applyViewModeChange(ViewMode previous)
{
    const uint32_t nowMs = millis();
    const bool leavingConfigEditView = previous != m_viewMode
        && isRuntimeConfigEditView(previous)
        && !isRuntimeConfigEditView(m_viewMode);

    if (previous == ViewMode::Image && m_viewMode != ViewMode::Image) {
        ImageSource::suspend();
    } else if (previous != ViewMode::Image && m_viewMode == ViewMode::Image && m_board != nullptr) {
        ImageSource::begin(*m_board);
    }

    if (previous != m_viewMode && (previous == ViewMode::Radar || m_viewMode == ViewMode::Radar)) {
        m_renderer.forceNextRadarFullFrame();
    }
    if (m_viewMode == ViewMode::DayClock && previous != ViewMode::DayClock) {
        m_lastDayClockMinuteKey = -1;
        if (!m_dayClockWeather.ok) {
            m_nextDayClockWeatherFetchMs = 0;
        }
    }

    if (m_viewMode != ViewMode::Radar) {
        closePlanePopup();
    }

    if (m_viewMode == ViewMode::Clock) {
        m_hasClockRenderPose = false;
        m_clockMotionActive = true;
        m_lastClockImuUpdateMs = 0;
        m_lastClockMotionMs = nowMs;
        suspendWiFiForClock();
    } else if (m_viewMode == ViewMode::DayClock) {
        if (m_runtimeConfig.hasWiFi()
            && (!m_dayClockWeather.ok || nowMs >= m_nextDayClockWeatherFetchMs)) {
            resumeWiFiAfterClock();
        } else {
            suspendWiFiForClock();
        }
    } else if (m_viewMode == ViewMode::Sky || m_viewMode == ViewMode::Watch
               || m_viewMode == ViewMode::AlternativeClock || m_viewMode == ViewMode::Image
               || m_viewMode == ViewMode::PomodoroControl || m_viewMode == ViewMode::PomodoroSettings) {
        suspendWiFiForClock();
    } else {
        resumeWiFiAfterClock();
        if (m_viewMode == ViewMode::Radar
            && (previous == ViewMode::Clock || previous == ViewMode::Sky || previous == ViewMode::Watch
                || previous == ViewMode::Options || previous == ViewMode::AlternativeClock
                || previous == ViewMode::DayClock || previous == ViewMode::Image
                || previous == ViewMode::PomodoroControl || previous == ViewMode::PomodoroSettings)) {
            m_radarViewEnteredMs = nowMs;
            m_wifiDisconnectedSinceMs = 0;

            // Volver al radar debe ser un restore visual, no un nuevo barrido ni
            // una captura inmediata. El snapshot de datos/labels sigue ligado al
            // cruce por norte. Esto evita el efecto de "todo brilla" al regresar
            // desde reloj/opciones/sky por hacer full-frame + refresco de labels.
            m_model.settleAfterHidden(nowMs);
            m_renderer.clearRadarLabels();
            m_hasRadarSweepAngle = false;
            // Rehacemos solo las labels usando los contactos ya disponibles en el modelo.
            // No pedimos API ni snapshot de red: simplemente evitamos volver al radar
            // sin etiquetas hasta el siguiente cruce por norte.
            m_refreshRadarLabelsOnNextRender = true;
            m_radarLabelNorthPassesSinceRefresh = 0;
            m_lastWifiReconnectAttemptMs = 0;
        }
    }

    if (leavingConfigEditView && m_configSavePending) {
        requestRuntimeConfigSave(nowMs);
    }
}

void RadarApp::showFaceView()
{
    const ViewMode previous = m_viewMode;
    m_currentColumn = 3;
    m_columnRow[m_currentColumn] = 1;
    m_viewMode = ViewMode::Image;
    applyViewModeChange(previous);
    m_touchIgnoreUntilMs = millis() + 300UL;
    m_lastRenderMs = 0;
}

void RadarApp::changeRangeStep(int delta)
{
    float current = m_runtimeConfig.rangeKm;
    float step = AppConfig::kRangeOptionStepKm;
    float next = current + (static_cast<float>(delta) * step);
    if (next < AppConfig::kMinRangeKm) next = AppConfig::kMaxRangeKm;
    if (next > AppConfig::kMaxRangeKm) next = AppConfig::kMinRangeKm;
    m_runtimeConfig.rangeKm = next;

    // Cambiar la distancia desde Opciones es un cambio de escala visual, no un
    // cambio de posición real. Por eso no usamos setOwnship(), que limpia los
    // aviones, y mantenemos los últimos contactos hasta que llegue la API.
    m_model.setRangeKm(m_runtimeConfig.rangeKm);
    m_model.setFetchRangeKm(trafficFetchRangeKmForTheme(m_runtimeConfig.rangeKm, m_runtimeConfig.radarTheme));
    persistRuntimeConfig();
}

void RadarApp::changeApiRefreshStep(int delta)
{
    int current = static_cast<int>(m_runtimeConfig.apiRefreshSeconds);
    current += delta * AppConfig::kApiRefreshStepSeconds;
    if (current > AppConfig::kMaxApiRefreshSeconds) current = AppConfig::kMinApiRefreshSeconds;
    if (current < AppConfig::kMinApiRefreshSeconds) current = AppConfig::kMaxApiRefreshSeconds;
    m_runtimeConfig.apiRefreshSeconds = static_cast<uint8_t>(current);
    m_runtimeConfig.normalize();
    m_model.setSweepPeriodSeconds(m_runtimeConfig.apiRefreshSeconds);
    m_nextApiFetchMs = millis() + apiRefreshMs();
    m_hasRadarSweepAngle = false;
    m_refreshRadarLabelsOnNextRender = true;
    m_radarLabelNorthPassesSinceRefresh = 0;
    persistRuntimeConfig();
}

void RadarApp::changeFpsStep(int delta)
{
    int current = static_cast<int>(m_runtimeConfig.fps) + (delta * AppConfig::kFpsStep);
    if (current > AppConfig::kMaxFps) current = AppConfig::kMinFps;
    if (current < AppConfig::kMinFps) current = AppConfig::kMaxFps;
    m_runtimeConfig.fps = static_cast<uint8_t>(current);
    m_runtimeConfig.normalize();
    persistRuntimeConfig();
}

void RadarApp::changeGyroFpsStep(int delta)
{
    constexpr uint8_t options[] = {7, 10, 15, 20, 25, 30, 35};
    int index = 0;
    for (int i = 0; i < static_cast<int>(sizeof(options)); ++i) {
        if (m_runtimeConfig.gyroFps <= options[i]) {
            index = i;
            break;
        }
    }
    index += delta;
    if (index < 0) {
        index = static_cast<int>(sizeof(options)) - 1;
    } else if (index >= static_cast<int>(sizeof(options))) {
        index = 0;
    }
    m_runtimeConfig.gyroFps = options[index];
    m_runtimeConfig.normalize();
    persistRuntimeConfig();
}

void RadarApp::changeRadarThemeStep(int delta)
{
    int current = static_cast<int>(m_runtimeConfig.radarTheme) + delta;
    if (current > AppConfig::kRadarThemeDragonBall) current = AppConfig::kRadarThemeGreen;
    if (current < AppConfig::kRadarThemeGreen) current = AppConfig::kRadarThemeDragonBall;
    m_runtimeConfig.radarTheme = static_cast<uint8_t>(current);
    m_model.setFetchRangeKm(trafficFetchRangeKmForTheme(m_runtimeConfig.rangeKm, m_runtimeConfig.radarTheme));
    persistRuntimeConfig();
}

void RadarApp::setAlarmEnabled(bool enabled)
{
    if (m_runtimeConfig.alarmEnabled == enabled) {
        return;
    }
    m_runtimeConfig.alarmEnabled = enabled;
    persistRuntimeConfig();
}

void RadarApp::setNorthBeepEnabled(bool enabled)
{
    if (m_runtimeConfig.northBeepEnabled == enabled) {
        return;
    }

    m_runtimeConfig.northBeepEnabled = enabled;
    m_hasNorthBeepSweepAngle = false;
    if (!enabled) {
        stopNorthBeep();
    }
    persistRuntimeConfig();
}

void RadarApp::cycleAlarmHourStep(int delta)
{
    int hour = static_cast<int>(m_runtimeConfig.alarmHour) + delta;
    if (hour > 23) hour = 0;
    if (hour < 0) hour = 23;
    if (m_runtimeConfig.alarmHour == static_cast<uint8_t>(hour)) {
        return;
    }
    m_runtimeConfig.alarmHour = static_cast<uint8_t>(hour);
    persistRuntimeConfig();
}

void RadarApp::cycleAlarmMinuteStep(int delta)
{
    int minute = static_cast<int>(m_runtimeConfig.alarmMinute) + (delta * 5);
    if (minute >= 60) minute = 0;
    if (minute < 0) minute = 55;
    if (m_runtimeConfig.alarmMinute == static_cast<uint8_t>(minute)) {
        return;
    }
    m_runtimeConfig.alarmMinute = static_cast<uint8_t>(minute);
    persistRuntimeConfig();
}

void RadarApp::applyBacklightBrightness(uint8_t brightness)
{
    const uint8_t clamped = static_cast<uint8_t>(std::clamp<int>(brightness, 0, 100));
    if (m_appliedBrightness == clamped) {
        return;
    }

    auto* backlight = m_board != nullptr ? m_board->getBacklight() : nullptr;
    if (backlight != nullptr) {
        backlight->setBrightness(static_cast<int>(clamped));
    }
    m_appliedBrightness = clamped;
}

void RadarApp::registerUserActivity(uint32_t nowMs)
{
    m_lastUserActivityMs = nowMs;
    if (m_idleSleepActive) {
        m_idleSleepActive = false;
        m_lastRenderMs = 0;
    }
    if (m_idleBrightnessDimmed) {
        m_idleBrightnessDimmed = false;
        applyBacklightBrightness(m_runtimeConfig.brightness);
    }
}

bool RadarApp::restoreBrightnessAfterIdle(uint32_t nowMs)
{
    if (!m_idleBrightnessDimmed) {
        return false;
    }

    registerUserActivity(nowMs);
    return true;
}

void RadarApp::updateIdleBrightness(uint32_t nowMs)
{
    // Atenuaci�n progresiva conmutable en RAM (por defecto activada). Si est�
    // desactivada, restauramos el brillo configurado si estaba atenuado.
    if (!m_runtimeConfig.idleDimEnabled) {
        if (m_idleBrightnessDimmed) {
            m_idleBrightnessDimmed = false;
            applyBacklightBrightness(m_runtimeConfig.brightness);
        }
        if (m_idleSleepActive) {
            m_idleSleepActive = false;
            m_lastRenderMs = 0;
        }
        return;
    }

    if (m_runtimeConfig.brightness <= kIdleDimMinBrightness) {
        if (m_idleSleepActive) {
            m_idleSleepActive = false;
            m_lastRenderMs = 0;
        }
        return;
    }

    if (m_lastUserActivityMs == 0) {
        m_lastUserActivityMs = nowMs;
        return;
    }

    const uint32_t idleMs = nowMs - m_lastUserActivityMs;
    if (idleMs < kIdleDimStartMs) {
        if (m_idleBrightnessDimmed) {
            m_idleBrightnessDimmed = false;
            applyBacklightBrightness(m_runtimeConfig.brightness);
        }
        if (m_idleSleepActive) {
            m_idleSleepActive = false;
            m_lastRenderMs = 0;
        }
        return;
    }

    const uint32_t dimSteps = 1UL + ((idleMs - kIdleDimStartMs) / kIdleDimStepMs);
    const int target = std::max<int>(kIdleDimMinBrightness,
        static_cast<int>(m_runtimeConfig.brightness) - static_cast<int>(dimSteps * kIdleDimStepPercent));

    if (target < static_cast<int>(m_runtimeConfig.brightness)) {
        if (!m_idleBrightnessDimmed) {
            ImageSource::notifyInteraction(ImageSource::Interaction::IdlePause, nowMs);
        }
        m_idleBrightnessDimmed = true;
        applyBacklightBrightness(static_cast<uint8_t>(target));
    }

    const bool canSleepBuddy = !m_pomodoroStarted
        && !m_waterReminderVisible
        && !m_stretchReminderVisible;
    const bool shouldSleepBuddy = canSleepBuddy && target <= static_cast<int>(kIdleDimMinBrightness);
    if (m_idleSleepActive != shouldSleepBuddy) {
        m_idleSleepActive = shouldSleepBuddy;
        m_lastRenderMs = 0;
    }
}

void RadarApp::setBrightness(uint8_t brightness)
{
    m_runtimeConfig.brightness = brightness;
    m_idleBrightnessDimmed = false;
    applyBacklightBrightness(m_runtimeConfig.brightness);
}

void RadarApp::cycleBrightnessStep(int delta)
{
    int current = static_cast<int>(m_runtimeConfig.brightness);
    current += delta * 10;
    if (current > AppConfig::kMaxBrightness) current = AppConfig::kMinBrightness;
    if (current < AppConfig::kMinBrightness) current = AppConfig::kMaxBrightness;
    setBrightness(static_cast<uint8_t>(current));
    persistRuntimeConfig();
}

void RadarApp::updateClockAlarm()
{
    if (!m_alarmOutputReady) return;

    if (m_alarmRinging) {
        const uint32_t nowMs = millis();
        if (nowMs - m_alarmStartMs >= AppConfig::kAlarmAutoStopMs) {
            dismissClockAlarm();
        } else if (nowMs - m_lastAlarmBeepMs >= AppConfig::kAlarmBeepIntervalMs) {
            m_lastAlarmBeepMs = nowMs;
            m_alarmBeepOn = !m_alarmBeepOn;
            syncBuzzerOutput();
        }
    }

    if (!m_runtimeConfig.alarmEnabled) {
        if (m_alarmRinging) dismissClockAlarm();
        return;
    }

    time_t now = time(nullptr);
    tm localInfo{};
    if (now < 100000 || localtime_r(&now, &localInfo) == nullptr) return;

    const int alarmKey = localInfo.tm_hour * 60 + localInfo.tm_min;
    const int targetKey = static_cast<int>(m_runtimeConfig.alarmHour) * 60 + static_cast<int>(m_runtimeConfig.alarmMinute);

    if (alarmKey == targetKey) {
        if (localInfo.tm_sec < 5 && !m_alarmRinging && m_lastAlarmKey != targetKey) {
            startClockAlarm(targetKey);
        }
    } else {
        m_lastAlarmKey = -1;
    }
}

void RadarApp::startClockAlarm(int alarmKey)
{
    m_alarmRinging = true;
    m_lastAlarmKey = alarmKey;
    m_alarmStartMs = millis();
    m_lastAlarmBeepMs = m_alarmStartMs;
    m_alarmBeepOn = true;
    m_buzzerCue = BuzzerCue::None;
    m_buzzerCueOn = false;
    syncBuzzerOutput();
}

void RadarApp::dismissClockAlarm()
{
    m_alarmRinging = false;
    m_alarmBeepOn = false;
    syncBuzzerOutput();
}

uint8_t RadarApp::buzzerCueStepCount(BuzzerCue cue) const
{
    switch (cue) {
        case BuzzerCue::PomodoroStart:
            return sizeof(kBuzzerStartCue) / sizeof(kBuzzerStartCue[0]);
        case BuzzerCue::PomodoroPause:
            return sizeof(kBuzzerPauseCue) / sizeof(kBuzzerPauseCue[0]);
        case BuzzerCue::PomodoroReset:
            return sizeof(kBuzzerResetCue) / sizeof(kBuzzerResetCue[0]);
        case BuzzerCue::WaterReminder:
            return sizeof(kBuzzerWaterCue) / sizeof(kBuzzerWaterCue[0]);
        case BuzzerCue::EyesReminder:
            return sizeof(kBuzzerEyesCue) / sizeof(kBuzzerEyesCue[0]);
        case BuzzerCue::FocusComplete:
            return sizeof(kBuzzerFocusCompleteCue) / sizeof(kBuzzerFocusCompleteCue[0]);
        case BuzzerCue::BreakComplete:
            return sizeof(kBuzzerBreakCompleteCue) / sizeof(kBuzzerBreakCompleteCue[0]);
        case BuzzerCue::None:
        default:
            return 0;
    }
}

uint16_t RadarApp::buzzerCueStepMs(BuzzerCue cue, uint8_t stepIndex) const
{
    switch (cue) {
        case BuzzerCue::PomodoroStart:
            return kBuzzerStartCue[stepIndex];
        case BuzzerCue::PomodoroPause:
            return kBuzzerPauseCue[stepIndex];
        case BuzzerCue::PomodoroReset:
            return kBuzzerResetCue[stepIndex];
        case BuzzerCue::WaterReminder:
            return kBuzzerWaterCue[stepIndex];
        case BuzzerCue::EyesReminder:
            return kBuzzerEyesCue[stepIndex];
        case BuzzerCue::FocusComplete:
            return kBuzzerFocusCompleteCue[stepIndex];
        case BuzzerCue::BreakComplete:
            return kBuzzerBreakCompleteCue[stepIndex];
        case BuzzerCue::None:
        default:
            return 0;
    }
}

void RadarApp::playBuzzerCue(BuzzerCue cue, uint32_t nowMs)
{
    if (!m_pomodoroSoundEnabled) {
        return;
    }
    const uint8_t stepCount = buzzerCueStepCount(cue);
    if (!m_alarmOutputReady || m_alarmRinging || stepCount == 0) {
        return;
    }

    m_buzzerCue = cue;
    m_buzzerCueStep = 0;
    m_buzzerCueOn = true;
    m_buzzerCueNextMs = nowMs + buzzerCueStepMs(cue, 0);
    syncBuzzerOutput();
}

void RadarApp::updateBuzzerCue(uint32_t nowMs)
{
    if (m_buzzerCue == BuzzerCue::None) {
        return;
    }

    if (m_alarmRinging) {
        m_buzzerCue = BuzzerCue::None;
        m_buzzerCueOn = false;
        syncBuzzerOutput();
        return;
    }

    while (m_buzzerCue != BuzzerCue::None
        && static_cast<int32_t>(nowMs - m_buzzerCueNextMs) >= 0) {
        ++m_buzzerCueStep;
        const uint8_t stepCount = buzzerCueStepCount(m_buzzerCue);
        if (m_buzzerCueStep >= stepCount) {
            m_buzzerCue = BuzzerCue::None;
            m_buzzerCueOn = false;
            syncBuzzerOutput();
            return;
        }

        m_buzzerCueOn = (m_buzzerCueStep % 2U) == 0;
        m_buzzerCueNextMs += buzzerCueStepMs(m_buzzerCue, m_buzzerCueStep);
        syncBuzzerOutput();
    }
}

void RadarApp::updateNorthBeep(uint32_t nowMs, bool radarView)
{
    const bool dragonBallRadar = radarView
        && m_runtimeConfig.radarTheme == AppConfig::kRadarThemeDragonBall;
    if (dragonBallRadar) {
        m_hasNorthBeepSweepAngle = false;
        stopNorthBeep();
        return;
    }

    if (!radarView || !m_runtimeConfig.northBeepEnabled || !m_alarmOutputReady || m_alarmRinging) {
        m_hasNorthBeepSweepAngle = false;
        stopNorthBeep();
        return;
    }

    if (m_northBeepActive && nowMs >= m_northBeepEndMs) {
        m_northBeepActive = false;
        syncBuzzerOutput();
    }

    const float currentAngle = m_model.sweepAngleDeg();
    if (!m_hasNorthBeepSweepAngle) {
        m_lastNorthBeepSweepAngle = currentAngle;
        m_hasNorthBeepSweepAngle = true;
        return;
    }

    const bool crossedNorth = currentAngle < m_lastNorthBeepSweepAngle;
    m_lastNorthBeepSweepAngle = currentAngle;

    if (crossedNorth && !m_northBeepActive) {
        m_northBeepActive = true;
        m_northBeepEndMs = nowMs + AppConfig::kNorthBeepPulseMs;
        syncBuzzerOutput();
    }
}

void RadarApp::stopNorthBeep()
{
    if (!m_northBeepActive) {
        return;
    }

    m_northBeepActive = false;
    syncBuzzerOutput();
}

void RadarApp::startDragonBallTouchBeep(uint32_t nowMs)
{
    if (m_alarmRinging) {
        return;
    }

    m_dragonBallTouchBeepActive = true;
    m_dragonBallTouchBeepOn = false;
    m_dragonBallTouchBeepStartMs = nowMs;
}

void RadarApp::updateDragonBallTouchBeep(uint32_t nowMs)
{
    const bool validMode = m_mode == Mode::Radar
        && m_viewMode == ViewMode::Radar
        && m_runtimeConfig.radarTheme == AppConfig::kRadarThemeDragonBall
        && m_runtimeConfig.northBeepEnabled
        && !m_alarmRinging;
    const uint32_t scanBeepDurationMs = static_cast<uint32_t>(AppConfig::kDragonBallWaveCount)
        * AppConfig::kDragonBallWaveDurationMs;

    if (!m_dragonBallTouchBeepActive || !validMode
        || (nowMs - m_dragonBallTouchBeepStartMs) >= scanBeepDurationMs) {
        stopDragonBallTouchBeep();
        return;
    }

    const uint32_t elapsedMs = nowMs - m_dragonBallTouchBeepStartMs;
    const uint32_t firstBeepDelayMs = AppConfig::kDragonBallScanBeepPeriodMs / 2;
    if (elapsedMs < firstBeepDelayMs) {
        if (m_dragonBallTouchBeepOn) {
            m_dragonBallTouchBeepOn = false;
            syncBuzzerOutput();
        }
        return;
    }

    const uint32_t cycleMs = (elapsedMs - firstBeepDelayMs)
        % AppConfig::kDragonBallScanBeepPeriodMs;
    const bool firstPulse = cycleMs < AppConfig::kDragonBallScanFirstBeepMs;
    const uint32_t secondPulseStart = AppConfig::kDragonBallScanFirstBeepMs
        + AppConfig::kDragonBallScanBeepGapMs;
    const bool secondPulse = cycleMs >= secondPulseStart
        && cycleMs < (secondPulseStart + AppConfig::kDragonBallScanSecondBeepMs);
    const bool beepOn = firstPulse || secondPulse;

    if (m_dragonBallTouchBeepOn != beepOn) {
        m_dragonBallTouchBeepOn = beepOn;
        syncBuzzerOutput();
    }
}

void RadarApp::stopDragonBallTouchBeep()
{
    if (!m_dragonBallTouchBeepActive && !m_dragonBallTouchBeepOn) {
        return;
    }

    m_dragonBallTouchBeepActive = false;
    m_dragonBallTouchBeepOn = false;
    syncBuzzerOutput();
}

void RadarApp::initAlarmOutput()
{
    auto* ioExpander = m_board->getIO_Expander();
    if (ioExpander != nullptr) {
        auto* base = ioExpander->getBase();
        if (base != nullptr) {
            base->pinMode(AppConfig::kBuzzerExpanderPin, OUTPUT);
            base->digitalWrite(AppConfig::kBuzzerExpanderPin, LOW);
            m_alarmOutputReady = true;
        }
    }
}

void RadarApp::setAlarmOutput(bool on)
{
    if (m_alarmOutputReady) {
        auto* ioExpander = m_board->getIO_Expander();
        if (ioExpander != nullptr) {
            auto* base = ioExpander->getBase();
            if (base != nullptr) {
                base->digitalWrite(AppConfig::kBuzzerExpanderPin, on ? HIGH : LOW);
            }
        }
    }
}

void RadarApp::syncBuzzerOutput()
{
    setAlarmOutput(m_alarmBeepOn || m_buzzerCueOn || m_northBeepActive || m_dragonBallTouchBeepOn);
}

void RadarApp::setRadarStatus(const String& statusText)
{
    m_radarStatusText = statusText;
    m_radarStatusUntilMs = millis() + AppConfig::kRadarStatusOverlayMs;
}

String RadarApp::htmlEscape(const String& value) const
{
    String result;
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            default: result += c; break;
        }
    }
    return result;
}

uint32_t RadarApp::millisUntilNextNorth(float currentSweepAngleDeg) const
{
    float angle = fmodf(currentSweepAngleDeg, 360.0f);
    if (angle < 0.0f) {
        angle += 360.0f;
    }

    float degreesToNorth = 360.0f - angle;
    if (degreesToNorth >= 359.95f) {
        degreesToNorth = 0.0f;
    }

    const float sweepPeriodMs = static_cast<float>(apiRefreshMs());
    return static_cast<uint32_t>(lroundf((degreesToNorth / 360.0f) * sweepPeriodMs));
}

void RadarApp::setRadarWiFiActiveForSnapshot(bool active)
{
    if (!m_runtimeConfig.hasWiFi()) {
        return;
    }

    if (active) {
        if (m_clockWifiSuspended) {
            m_clockWifiSuspended = false;
            WiFi.mode(WIFI_STA);
        }
        WiFi.setSleep(false);
        WiFi.setTxPower(WIFI_POWER_15dBm);
        m_radarWifiAwakeForSnapshot = true;
        m_lastRadarWifiPrewarmMs = millis();
        return;
    }

    if (!m_radarWifiAwakeForSnapshot) {
        return;
    }
    if (m_fetchInProgress || m_networkTaskHandle != nullptr) {
        return;
    }

    applyStationPowerSaving();
    m_radarWifiAwakeForSnapshot = false;
}

void RadarApp::prepareRadarWiFiForNorth(uint32_t nowMs)
{
    if (m_mode != Mode::Radar
        || m_viewMode != ViewMode::Radar
        || m_planePopupOpen
        || m_runtimeConfig.radarTheme == AppConfig::kRadarThemeDragonBall
        || !m_runtimeConfig.hasWiFi()) {
        setRadarWiFiActiveForSnapshot(false);
        return;
    }

    const uint32_t untilNorthMs = millisUntilNextNorth(m_model.sweepAngleDeg());
    if (untilNorthMs <= AppConfig::kRadarWifiPrewarmMs || m_fetchInProgress || m_networkTaskHandle != nullptr) {
        setRadarWiFiActiveForSnapshot(true);
        return;
    }

    if (m_radarWifiAwakeForSnapshot
        && (nowMs - m_lastRadarWifiPrewarmMs) >= AppConfig::kRadarWifiPowerSaveCooldownMs) {
        setRadarWiFiActiveForSnapshot(false);
    }
}

bool RadarApp::detectRadarNorthCrossing(float currentSweepAngleDeg)
{
    if (!m_hasRadarSweepAngle) {
        m_hasRadarSweepAngle = true;
        m_lastRadarSweepAngleDeg = currentSweepAngleDeg;
        return false;
    }

    const float previous = m_lastRadarSweepAngleDeg;
    m_lastRadarSweepAngleDeg = currentSweepAngleDeg;

    // Norte = 0/360 grados. Detectamos el wrap de 360 -> 0. Usamos una ventana
    // amplia para no perder el cruce si un frame se retrasa un poco.
    return previous >= 270.0f && currentSweepAngleDeg <= 90.0f;
}

void RadarApp::requestRadarSnapshotAtNorth(uint32_t nowMs)
{
    if (m_mode == Mode::Radar
        && m_viewMode == ViewMode::Radar
        && !m_planePopupOpen
        && m_runtimeConfig.radarTheme != AppConfig::kRadarThemeDragonBall) {
        if ((m_radarLabelNorthPassesSinceRefresh + 1U) >= kRadarLabelRebuildNorthPasses) {
            m_refreshRadarLabelsOnNextRender = true;
            m_radarLabelNorthPassesSinceRefresh = 0;
        } else {
            ++m_radarLabelNorthPassesSinceRefresh;
        }
    }

    if (m_mode != Mode::Radar
        || m_viewMode != ViewMode::Radar
        || m_planePopupOpen
        || m_runtimeConfig.radarTheme == AppConfig::kRadarThemeDragonBall
        || !m_runtimeConfig.hasWiFi()) {
        return;
    }

    // Protección por si el frame rate cae y se detecta más de una vez el mismo
    // cruce. Como el periodo visual del barrido está ligado al API refresh, esto
    // mantiene una petición por vuelta.
    const uint32_t minSnapshotGapMs = std::max<uint32_t>(1000UL, apiRefreshMs() / 2UL);
    if (m_lastRadarNorthSnapshotMs != 0 && (nowMs - m_lastRadarNorthSnapshotMs) < minSnapshotGapMs) {
        return;
    }

    if (m_fetchInProgress || m_networkTaskHandle != nullptr) {
        return;
    }

    m_lastRadarNorthSnapshotMs = nowMs;
    m_lastWifiReconnectAttemptMs = nowMs;
    scheduleNetworkFetch(nowMs, apiRefreshMs(), true, false);
}

uint32_t RadarApp::apiRefreshMs() const
{
    return static_cast<uint32_t>(m_runtimeConfig.apiRefreshSeconds) * 1000UL;
}

void RadarApp::handleRootRequest()
{
    m_webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    m_webServer.sendHeader("Pragma", "no-cache");
    m_webServer.sendHeader("Expires", "0");

    // ── CSS (stored in flash) ──────────────────────────────────────────
    static const char kCss[] PROGMEM =
        ":root{--green:#00ff7a;--green-dim:#00a050;--amber:#ffb000;--red:#ff5d5d;"
        "--bg:#020806;--grid:rgba(0,255,122,.08);--line:rgba(0,255,122,.45);"
        "--mono:ui-monospace,'Cascadia Mono','Consolas','Courier New',monospace}"
        "*{box-sizing:border-box}"
        "html,body{margin:0;padding:0;background:var(--bg);color:var(--green);font-family:var(--mono)}"
        "body{min-height:100vh;padding:22px 14px 70px;"
        "background:radial-gradient(ellipse at center,#031410 0%,#000 75%),"
        "linear-gradient(var(--grid) 1px,transparent 1px) 0 0/40px 40px,"
        "linear-gradient(90deg,var(--grid) 1px,transparent 1px) 0 0/40px 40px;"
        "background-color:#000;overflow-x:hidden;position:relative}"
        "body::before{content:\"\";position:fixed;inset:0;pointer-events:none;z-index:5;"
        "background:repeating-linear-gradient(180deg,rgba(0,0,0,0) 0,rgba(0,0,0,0) 2px,rgba(0,40,20,.35) 3px,rgba(0,0,0,0) 4px);"
        "mix-blend-mode:multiply}"
        "body::after{content:\"\";position:fixed;inset:0;pointer-events:none;z-index:6;"
        "background:radial-gradient(ellipse at center,transparent 55%,rgba(0,0,0,.85) 100%);animation:flk 5s infinite}"
        "@keyframes flk{0%,100%{opacity:1}48%{opacity:.96}50%{opacity:.84}52%{opacity:.97}}"
        ".wrap{max-width:760px;margin:0 auto;position:relative;z-index:1}"
        "header{display:flex;align-items:center;justify-content:space-between;gap:12px;"
        "border:1px solid var(--line);padding:14px 18px;margin-bottom:20px;"
        "background:linear-gradient(180deg,rgba(0,40,20,.45),rgba(0,10,5,.6));"
        "box-shadow:0 0 18px rgba(0,255,122,.18) inset,0 0 20px rgba(0,255,122,.15);"
        "position:relative;overflow:hidden}"
        "header::before{content:\"\";position:absolute;inset:0;pointer-events:none;"
        "background:repeating-linear-gradient(90deg,transparent 0,transparent 8px,rgba(0,255,122,.05) 8px,rgba(0,255,122,.05) 9px)}"
        "h1{font-family:var(--mono);font-size:1.6rem;font-weight:700;letter-spacing:.22em;margin:0;color:var(--green);"
        "text-shadow:0 0 8px var(--green),0 0 18px rgba(0,255,122,.6),0 0 2px var(--green)}"
        "h1 .bk{animation:bk 1s steps(1) infinite;color:var(--amber);text-shadow:0 0 8px var(--amber)}"
        "@keyframes bk{50%{opacity:0}}"
        ".status{font-family:var(--mono);font-weight:700;font-size:.85rem;color:var(--amber);letter-spacing:.18em;"
        "text-shadow:0 0 6px var(--amber);display:flex;align-items:center;gap:8px;white-space:nowrap}"
        ".dot{width:10px;height:10px;border-radius:50%;background:var(--amber);"
        "box-shadow:0 0 10px var(--amber),0 0 18px var(--amber);animation:pls 1.4s infinite ease-in-out}"
        "@keyframes pls{0%,100%{transform:scale(1);opacity:1}50%{transform:scale(1.4);opacity:.45}}"
        ".radar{position:absolute;right:-70px;top:-70px;width:220px;height:220px;border-radius:50%;"
        "border:1px solid rgba(0,255,122,.25);box-shadow:inset 0 0 30px rgba(0,255,122,.2);"
        "opacity:.55;pointer-events:none;z-index:0;overflow:hidden}"
        ".radar::before,.radar::after{content:\"\";position:absolute;inset:30px;border-radius:50%;border:1px solid rgba(0,255,122,.22)}"
        ".radar::after{inset:60px}"
        ".sweep{position:absolute;inset:0;border-radius:50%;"
        "background:conic-gradient(from 0deg,transparent 0deg,rgba(0,255,122,.55) 30deg,transparent 60deg);"
        "animation:spin 4s linear infinite;filter:blur(1px)}"
        "@keyframes spin{to{transform:rotate(360deg)}}"
        "fieldset{border:1px solid var(--line);margin:18px 0;padding:14px 16px 18px;position:relative;"
        "background:linear-gradient(180deg,rgba(0,30,18,.35),rgba(0,10,5,.55));"
        "box-shadow:0 0 16px rgba(0,255,122,.08) inset,0 0 14px rgba(0,255,122,.06)}"
        "fieldset::before,fieldset::after{content:\"\";position:absolute;width:14px;height:14px;border:2px solid var(--amber)}"
        "fieldset::before{top:-2px;left:-2px;border-right:none;border-bottom:none}"
        "fieldset::after{bottom:-2px;right:-2px;border-left:none;border-top:none}"
        "legend{font-family:var(--mono);font-weight:700;font-size:.95rem;color:var(--amber);"
        "letter-spacing:.2em;padding:0 10px;text-shadow:0 0 6px var(--amber)}"
        "legend::before{content:\"[ \"}legend::after{content:\" ]\"}"
        "label{display:block;margin:12px 0 4px;font-size:.72rem;font-weight:700;letter-spacing:.22em;color:var(--green-dim);text-transform:uppercase}"
        "label::before{content:\"> \"}"
        "input:not([type=checkbox]):not([type=submit]){width:100%;padding:9px 12px;background:rgba(0,15,8,.7);"
        "color:var(--green);border:1px solid rgba(0,255,122,.35);"
        "font-family:var(--mono);font-size:.95rem;outline:none;letter-spacing:.06em;"
        "caret-color:var(--amber);transition:border-color .2s,box-shadow .2s,background .2s,color .2s;"
        "text-shadow:0 0 6px rgba(0,255,122,.6)}"
        "input:not([type=checkbox]):not([type=submit]):focus{border-color:var(--amber);background:rgba(30,20,0,.55);"
        "box-shadow:0 0 0 1px var(--amber),0 0 16px rgba(255,176,0,.45);color:var(--amber);text-shadow:0 0 6px var(--amber)}"
        "input::placeholder{color:rgba(0,255,122,.35)}"
        "label.check{display:flex;align-items:center;gap:10px;margin:6px 0;font-size:.88rem;color:var(--green);"
        "letter-spacing:.08em;text-transform:none;font-weight:400;cursor:pointer;padding:6px 8px;border:1px dashed transparent;"
        "transition:border-color .2s,background .2s}"
        "label.check::before{content:\"\"}"
        "label.check:hover{border-color:rgba(0,255,122,.3);background:rgba(0,255,122,.04)}"
        "label.check input{appearance:none;-webkit-appearance:none;width:18px;height:18px;"
        "border:1px solid var(--green);background:rgba(0,15,8,.6);position:relative;cursor:pointer;flex-shrink:0;"
        "transition:box-shadow .2s,background .2s}"
        "label.check input:checked{background:rgba(0,255,122,.15);"
        "box-shadow:0 0 10px rgba(0,255,122,.6),inset 0 0 8px rgba(0,255,122,.4)}"
        "label.check input:checked::after{content:\"\";position:absolute;inset:3px;background:var(--green);box-shadow:0 0 8px var(--green)}"
        ".note{font-size:.78rem;color:rgba(0,255,122,.55);margin-top:8px;letter-spacing:.04em;"
        "padding-left:12px;border-left:2px solid rgba(0,255,122,.25);line-height:1.4}"
        ".ok{color:var(--green);font-weight:700;text-shadow:0 0 6px var(--green)}"
        ".ko{color:var(--red);font-weight:700;text-shadow:0 0 6px var(--red)}"
        ".actions{margin-top:26px;display:flex;justify-content:center}"
        "input[type=submit]{width:auto;appearance:none;-webkit-appearance:none;"
        "background:linear-gradient(180deg,rgba(255,176,0,.18),rgba(255,176,0,.05));"
        "color:var(--amber);border:1px solid var(--amber);padding:13px 36px;"
        "font-family:var(--mono);font-weight:700;font-size:1.05rem;letter-spacing:.3em;cursor:pointer;"
        "text-shadow:0 0 8px var(--amber);"
        "box-shadow:0 0 18px rgba(255,176,0,.25),inset 0 0 12px rgba(255,176,0,.15);"
        "transition:all .25s;position:relative}"
        "input[type=submit]:hover{background:linear-gradient(180deg,rgba(255,176,0,.38),rgba(255,176,0,.1));"
        "box-shadow:0 0 30px rgba(255,176,0,.7),inset 0 0 16px rgba(255,176,0,.3);transform:translateY(-1px)}"
        "input[type=submit]:active{transform:translateY(1px)}"
        "footer{text-align:center;margin-top:22px;font-size:.75rem;color:rgba(0,255,122,.4);letter-spacing:.22em}"
        ".wrap>*{animation:boot .5s ease-out backwards}"
        ".wrap>*:nth-child(1){animation-delay:.05s}.wrap>*:nth-child(2){animation-delay:.18s}"
        ".wrap>*:nth-child(3){animation-delay:.30s}"
        "@keyframes boot{from{opacity:0;transform:translateY(8px);filter:blur(4px)}to{opacity:1;transform:translateY(0);filter:blur(0)}}"
        "@media(max-width:560px){h1{font-size:1.25rem;letter-spacing:.15em}.radar{display:none}header{flex-wrap:wrap}"
        "input[type=submit]{font-size:.95rem;letter-spacing:.22em;padding:12px 24px}}";

    // ── Build dynamic HTML ─────────────────────────────────────────────
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html lang=\"es\"><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>RADAR // SETUP</title><style>";
    html += FPSTR(kCss);
    html += "</style></head><body><div class=\"wrap\">"
        "<header>"
        "<h1>RADAR // SETUP<span class=\"bk\">_</span></h1>"
        "<div class=\"status\"><span class=\"dot\"></span>SYS ONLINE</div>"
        "<div class=\"radar\"><div class=\"sweep\"></div></div>"
        "</header>"
        "<form action=\"/save\" method=\"POST\">";

    // ── WiFi profiles ──────────────────────────────────────────────────
    for (int i = 0; i < AppConfig::kMaxWifiProfiles; ++i)
    {
        const bool hasPass = !m_runtimeConfig.wifiPassword[i].isEmpty();
        html += String("<fieldset><legend>WIFI ") + String(i + 1) + "</legend>"
            "<label>SSID</label><input type=\"text\" name=\"ssid" + String(i) + "\" value=\"" +
                htmlEscape(m_runtimeConfig.wifiSsid[i]) + "\" autocomplete=\"off\" spellcheck=\"false\">"
            "<label>PASSWORD</label><input type=\"password\" name=\"pass" + String(i) +
                "\" placeholder=\"vacio = no cambiar\" autocomplete=\"new-password\">"
            "<div class=\"note\">&gt; Password en flash: <span class=\"" +
                String(hasPass ? "ok\">SI" : "ko\">NO") + "</span></div>";
        if (i == 0)
        {
            html += "<div class=\"note\">&gt; SSID vacio borra el perfil. Password vacio mantiene la guardada"
                " solo si el SSID no cambia. Si solo usas WIFI 2, deja WIFI 1 en blanco.</div>";
        }
        html += "</fieldset>";
    }

    // ── Location ───────────────────────────────────────────────────────
    html += "<fieldset><legend>LOCALIZACION</legend>"
        "<label>LOCATION LABEL</label><input type=\"text\" name=\"loc\" value=\"" +
            htmlEscape(m_runtimeConfig.locationLabel) + "\" autocomplete=\"off\">"
        "<label>LATITUDE</label><input type=\"text\" name=\"lat\" value=\"" +
            String(m_runtimeConfig.ownshipLat, 6) + "\" inputmode=\"decimal\">"
        "<label>LONGITUDE</label><input type=\"text\" name=\"lon\" value=\"" +
            String(m_runtimeConfig.ownshipLon, 6) + "\" inputmode=\"decimal\">"
        "</fieldset>";


    // ── Radar / runtime defaults ───────────────────────────────────────
    const char* themeNames[] = {"Green", "Blue", "Cyan", "Red", "Dragon Ball"};
    html += "<fieldset><legend>RADAR</legend>";
    html += String("<label>RANGO KM</label><input type=\"number\" name=\"range\" min=\"")
        + String(static_cast<int>(AppConfig::kMinRangeKm))
        + "\" max=\"" + String(static_cast<int>(AppConfig::kMaxRangeKm))
        + "\" step=\"10\" value=\"" + String(static_cast<int>(m_runtimeConfig.rangeKm))
        + "\">";
    html += String("<label>API REFRESH SEGUNDOS</label><input type=\"number\" name=\"api\" min=\"")
        + String(AppConfig::kMinApiRefreshSeconds)
        + "\" max=\"" + String(AppConfig::kMaxApiRefreshSeconds)
        + "\" step=\"" + String(AppConfig::kApiRefreshStepSeconds)
        + "\" value=\"" + String(m_runtimeConfig.apiRefreshSeconds)
        + "\">";
    html += String("<label>FPS RADAR</label><input type=\"number\" name=\"fps\" min=\"")
        + String(AppConfig::kMinFps)
        + "\" max=\"" + String(AppConfig::kMaxFps)
        + "\" step=\"" + String(AppConfig::kFpsStep)
        + "\" value=\"" + String(m_runtimeConfig.fps)
        + "\">";
    html += String("<label>BRILLO</label><input type=\"number\" name=\"brightness\" min=\"")
        + String(AppConfig::kMinBrightness)
        + "\" max=\"" + String(AppConfig::kMaxBrightness)
        + "\" step=\"5\" value=\"" + String(m_runtimeConfig.brightness)
        + "\">";
    html += "<label>TEMA</label><select name=\"theme\">";
    for (uint8_t theme = AppConfig::kRadarThemeGreen; theme <= AppConfig::kRadarThemeDragonBall; ++theme)
    {
        html += String("<option value=\"") + String(theme) + "\""
            + (theme == m_runtimeConfig.radarTheme ? " selected" : "")
            + ">" + themeNames[theme] + "</option>";
    }
    html += "</select>";
    html += String("<label class=\"check\"><input type=\"checkbox\" name=\"mil\" value=\"1\"")
        + (m_runtimeConfig.militaryOnlyEnabled ? " checked" : "")
        + "> Modo militar / destacar militares</label>";
    html += String("<label class=\"check\"><input type=\"checkbox\" name=\"idle_dim\" value=\"1\"")
        + (m_runtimeConfig.idleDimEnabled ? " checked" : "")
        + "> Auto dim / dormir buddy sin timers</label>";
    html += "<div class=\"note\">&gt; Estos valores quedan guardados; luego puedes ajustarlos desde OPTIONS.</div></fieldset>";

    html += "<fieldset><legend>POMODORO</legend>";
    html += String("<label>FOCUS MINUTOS</label><input type=\"number\" name=\"pomo_focus\" min=\"25\" max=\"50\" step=\"5\" value=\"")
        + String(m_runtimeConfig.pomodoroFocusMinutes) + "\">";
    html += String("<label>BREAK MINUTOS</label><input type=\"number\" name=\"pomo_break\" min=\"5\" max=\"15\" step=\"5\" value=\"")
        + String(m_runtimeConfig.pomodoroBreakMinutes) + "\">";
    html += String("<label>LONG BREAK MINUTOS</label><input type=\"number\" name=\"pomo_long\" min=\"15\" max=\"30\" step=\"5\" value=\"")
        + String(m_runtimeConfig.pomodoroLongBreakMinutes) + "\">";
    html += String("<label>RECORDAR AGUA MIN</label><input type=\"number\" name=\"rem_water\" min=\"0\" max=\"90\" step=\"5\" value=\"")
        + String(m_runtimeConfig.waterReminderMinutes) + "\">";
    html += String("<label>RECORDAR VISTA/ESTIRAR MIN</label><input type=\"number\" name=\"rem_stretch\" min=\"0\" max=\"60\" step=\"5\" value=\"")
        + String(m_runtimeConfig.stretchReminderMinutes) + "\">";
    html += String("<label class=\"check\"><input type=\"checkbox\" name=\"pomo_sound\" value=\"1\"")
        + (m_runtimeConfig.pomodoroSoundEnabled ? " checked" : "")
        + "> Sonidos Pomodoro / recordatorios</label>";
    html += "<div class=\"note\">&gt; 0 desactiva el recordatorio.</div></fieldset>";

    // ── View toggles (VENTANAS) ────────────────────────────────────────
    struct ViewEntry { const char* name; const char* field; bool enabled; };
    const ViewEntry views[] = {
        {"Radar",              "v_radar",  m_runtimeConfig.viewRadarEnabled},
        {"Cielo",              "v_sky",    m_runtimeConfig.viewSkyEnabled},
        {"Reloj watch",        "v_watch",  m_runtimeConfig.viewWatchEnabled},
        {"Reloj clasico",      "v_clock",  m_runtimeConfig.viewClockEnabled},
        {"Reloj alternativo",  "v_altclk", m_runtimeConfig.viewAlternativeClockEnabled},
        {"IT'S TODAY",         "v_dayclk", m_runtimeConfig.viewDayClockEnabled},
        {"Opciones",           "v_opts",   m_runtimeConfig.viewOptionsEnabled},
        {"Imagen",              "v_image", m_runtimeConfig.viewImageEnabled},
    };
    html += "<fieldset><legend>VENTANAS</legend>"
        "<div class=\"note\">&gt; Las ventanas desactivadas se saltan al deslizar.</div>";
    for (const auto& v : views)
    {
        html += String("<label class=\"check\"><input type=\"checkbox\" name=\"") +
            v.field + "\" value=\"1\"" + (v.enabled ? " checked" : "") +
            "> " + v.name + "</label>";
    }
    html += "</fieldset>";

    // ── Submit + footer ────────────────────────────────────────────────
    html += String("<div class=\"actions\"><input type=\"submit\" value=\"SAVE &amp; RESTART\"></div>"
        "</form>"
        "<div class=\"note\" style=\"text-align:center;border:none;padding:0;margin-top:14px\">")
        + String("&gt; Al guardar, el dispositivo reinicia y abre la primera ventana activa.")
        + "</div>"
        "<footer>// ESP32 RADAR CTRL &middot; v1.0 //</footer>"
        "</div></body></html>";

    m_webServer.send(200, "text/html; charset=UTF-8", html);
}

void RadarApp::handleSaveRequest()
{
  auto clampInt = [](int value, int minValue, int maxValue) -> int {
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
  };

  auto parseDoubleArg = [this](const char* name, double currentValue, double minValue, double maxValue) -> double {
    if (!m_webServer.hasArg(name)) {
      return currentValue;
    }
    String value = m_webServer.arg(name);
    value.trim();
    if (value.isEmpty()) {
      return currentValue;
    }
    char* end = nullptr;
    const double parsed = strtod(value.c_str(), &end);
    if (end == value.c_str() || parsed < minValue || parsed > maxValue) {
      return currentValue;
    }
    return parsed;
  };

  for (int i = 0; i < AppConfig::kMaxWifiProfiles; ++i)
  {
    const String ssidArg = String("ssid") + String(i);
    const String passArg = String("pass") + String(i);

    const String previousSsid = m_runtimeConfig.wifiSsid[i];
    String submittedSsid = previousSsid;
    if (m_webServer.hasArg(ssidArg))
    {
      submittedSsid = m_webServer.arg(ssidArg);
      submittedSsid.trim();
      m_runtimeConfig.wifiSsid[i] = submittedSsid;
    }
    if (m_webServer.hasArg(passArg))
    {
      const String submittedPassword = m_webServer.arg(passArg);
      if (submittedPassword.length() > 0)
      {
        m_runtimeConfig.wifiPassword[i] = submittedPassword;
      }
      else if (m_runtimeConfig.wifiSsid[i].isEmpty() || submittedSsid != previousSsid)
      {
        m_runtimeConfig.wifiPassword[i] = "";
      }
    }
  }
  if (m_webServer.hasArg("loc"))
  {
    m_runtimeConfig.locationLabel = m_webServer.arg("loc");
  }
  m_runtimeConfig.ownshipLat = parseDoubleArg("lat", m_runtimeConfig.ownshipLat, -90.0, 90.0);
  m_runtimeConfig.ownshipLon = parseDoubleArg("lon", m_runtimeConfig.ownshipLon, -180.0, 180.0);

  if (m_webServer.hasArg("range"))
  {
    const int rangeKm = clampInt(m_webServer.arg("range").toInt(),
      static_cast<int>(AppConfig::kMinRangeKm), static_cast<int>(AppConfig::kMaxRangeKm));
    m_runtimeConfig.rangeKm = static_cast<float>(rangeKm);
  }
  if (m_webServer.hasArg("api"))
  {
    m_runtimeConfig.apiRefreshSeconds = static_cast<uint8_t>(clampInt(m_webServer.arg("api").toInt(),
      AppConfig::kMinApiRefreshSeconds, AppConfig::kMaxApiRefreshSeconds));
  }
  if (m_webServer.hasArg("fps"))
  {
    m_runtimeConfig.fps = static_cast<uint8_t>(clampInt(m_webServer.arg("fps").toInt(),
      AppConfig::kMinFps, AppConfig::kMaxFps));
    m_runtimeConfig.gyroFps = m_runtimeConfig.fps;
  }
  if (m_webServer.hasArg("brightness"))
  {
    m_runtimeConfig.brightness = static_cast<uint8_t>(clampInt(m_webServer.arg("brightness").toInt(),
      AppConfig::kMinBrightness, AppConfig::kMaxBrightness));
  }
  if (m_webServer.hasArg("theme"))
  {
    m_runtimeConfig.radarTheme = static_cast<uint8_t>(clampInt(m_webServer.arg("theme").toInt(),
      AppConfig::kRadarThemeGreen, AppConfig::kRadarThemeDragonBall));
  }
  m_runtimeConfig.militaryOnlyEnabled = m_webServer.hasArg("mil");
  m_runtimeConfig.idleDimEnabled = m_webServer.hasArg("idle_dim");

  if (m_webServer.hasArg("pomo_focus"))
  {
    m_runtimeConfig.pomodoroFocusMinutes = static_cast<uint8_t>(clampInt(m_webServer.arg("pomo_focus").toInt(), 25, 50));
  }
  if (m_webServer.hasArg("pomo_break"))
  {
    m_runtimeConfig.pomodoroBreakMinutes = static_cast<uint8_t>(clampInt(m_webServer.arg("pomo_break").toInt(), 5, 15));
  }
  if (m_webServer.hasArg("pomo_long"))
  {
    m_runtimeConfig.pomodoroLongBreakMinutes = static_cast<uint8_t>(clampInt(m_webServer.arg("pomo_long").toInt(), 15, 30));
  }
  if (m_webServer.hasArg("rem_water"))
  {
    m_runtimeConfig.waterReminderMinutes = static_cast<uint8_t>(clampInt(m_webServer.arg("rem_water").toInt(), 0, 90));
  }
  if (m_webServer.hasArg("rem_stretch"))
  {
    m_runtimeConfig.stretchReminderMinutes = static_cast<uint8_t>(clampInt(m_webServer.arg("rem_stretch").toInt(), 0, 60));
  }
  m_runtimeConfig.pomodoroSoundEnabled = m_webServer.hasArg("pomo_sound");

  // View toggles – unchecked checkboxes are absent from POST data.
  m_runtimeConfig.viewRadarEnabled            = m_webServer.hasArg("v_radar");
  m_runtimeConfig.viewSkyEnabled              = m_webServer.hasArg("v_sky");
  m_runtimeConfig.viewWatchEnabled            = m_webServer.hasArg("v_watch");
  m_runtimeConfig.viewClockEnabled            = m_webServer.hasArg("v_clock");
  m_runtimeConfig.viewAlternativeClockEnabled = m_webServer.hasArg("v_altclk");
  m_runtimeConfig.viewDayClockEnabled         = m_webServer.hasArg("v_dayclk");
  m_runtimeConfig.viewOptionsEnabled          = m_webServer.hasArg("v_opts");
  m_runtimeConfig.viewImageEnabled            = m_webServer.hasArg("v_image");

  m_runtimeConfig.normalize();
  m_pomodoroFocusMinutes = m_runtimeConfig.pomodoroFocusMinutes;
  m_pomodoroBreakMinutes = m_runtimeConfig.pomodoroBreakMinutes;
  m_pomodoroLongBreakMinutes = m_runtimeConfig.pomodoroLongBreakMinutes;
  m_waterReminderMinutes = m_runtimeConfig.waterReminderMinutes;
  m_stretchReminderMinutes = m_runtimeConfig.stretchReminderMinutes;
  m_pomodoroSoundEnabled = m_runtimeConfig.pomodoroSoundEnabled;
  m_pomodoroRemainingMs = pomodoroPhaseDurationMs();
  persistRuntimeConfig();
  const bool savedToSd = saveRuntimeConfigNow();
  m_webServer.send(200, "text/html; charset=UTF-8",
    "<!DOCTYPE html><html lang=\"es\"><head><meta charset=\"UTF-8\">"
    + String(savedToSd ? "<meta http-equiv=\"refresh\" content=\"5\">" : "")
    + "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>SAVED</title>"
    "<style>"
    ":root{--green:#00ff7a;--amber:#ffb000;"
    "--mono:ui-monospace,'Cascadia Mono','Consolas','Courier New',monospace}"
    "body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
    "background:#020806;color:var(--green);font-family:var(--mono);"
    "background:radial-gradient(ellipse at center,#031410 0%,#000 75%)}"
    ".box{text-align:center;border:1px solid rgba(0,255,122,.45);padding:40px 50px;"
    "background:linear-gradient(180deg,rgba(0,40,20,.45),rgba(0,10,5,.6));"
    "box-shadow:0 0 18px rgba(0,255,122,.18) inset,0 0 20px rgba(0,255,122,.15);"
    "animation:boot .5s ease-out}"
    "h1{font-size:1.5rem;letter-spacing:.25em;margin:0 0 12px;"
    "text-shadow:0 0 8px var(--green),0 0 18px rgba(0,255,122,.6)}"
    "p{color:var(--amber);letter-spacing:.15em;font-size:.95rem;"
    "text-shadow:0 0 6px var(--amber)}"
    "@keyframes boot{from{opacity:0;transform:scale(.96);filter:blur(4px)}"
    "to{opacity:1;transform:scale(1);filter:blur(0)}}"
    "</style></head><body>"
    "<div class=\"box\"><h1>"
    + String(savedToSd ? "CONFIGURATION SAVED" : "SD SAVE FAILED") + "</h1>"
    "<p>"
    + String(savedToSd ? "RESTARTING..." : "CONFIG ONLY IN RAM") + "</p></div></body></html>");
  m_restartRequested = savedToSd;
}

bool RadarApp::tryOpenPlanePopup(int x, int y)
{
    if (m_mode != Mode::Radar
        || m_viewMode != ViewMode::Radar
        || m_runtimeConfig.radarTheme == AppConfig::kRadarThemeDragonBall) {
        return false;
    }

    const int planeIndex = m_renderer.hitTestPlaneIndex(m_model, x, y, m_runtimeConfig.radarTheme);
    if (planeIndex < 0 || planeIndex >= static_cast<int>(m_model.visiblePlanes().size())) {
        return false;
    }

    // El Plane ya viene con pais resuelto desde RadarModel::applyFetchResult().
    // Evitamos repetir CountryResolver en cada apertura de popup.
    m_planePopupPlane = m_model.visiblePlanes()[planeIndex].plane;
    m_planePopupOpen = true;
    m_planePopupOpenedMs = millis();
    m_lastDeselectedPlaneId = "";
    m_lastDeselectedPlaneMarkerUntilMs = 0;
    m_lastRenderMs = 0;
    return true;
}

void RadarApp::closePlanePopup()
{
    const bool wasOpen = m_planePopupOpen;
    if (m_planePopupOpen && !m_planePopupPlane.id.isEmpty()) {
        m_lastDeselectedPlaneId = m_planePopupPlane.id;
        m_lastDeselectedPlaneMarkerUntilMs = millis() + kDeselectedPlaneMarkerMs;
    }
    m_planePopupOpen = false;
    m_planePopupOpenedMs = 0;
    m_planePopupPlane = Plane{};

    // Si el popup ha estado abierto varios segundos, los contactos visibles pueden
    // haber caducado/fadeado mientras las labels seguían congeladas. Al cerrarlo
    // limpiamos labels visibles y pendientes para evitar etiquetas huérfanas o
    // medio pintadas. Se reconstruirán de forma natural en el siguiente cruce norte.
    if (wasOpen) {
        m_renderer.clearRadarLabels();
        // Al cerrar popup reconstruimos las labels con los aviones que ya están
        // en memoria. No se fuerza red/API; solo se evita esperar otra vuelta
        // completa para volver a ver etiquetas.
        m_refreshRadarLabelsOnNextRender = true;
        m_radarLabelNorthPassesSinceRefresh = 0;
    }
}

bool RadarApp::isViewEnabled(ViewMode view) const
{
    switch (view) {
        case ViewMode::Radar: return m_runtimeConfig.viewRadarEnabled;
        case ViewMode::Sky: return m_runtimeConfig.viewSkyEnabled;
        case ViewMode::Watch: return m_runtimeConfig.viewWatchEnabled;
        case ViewMode::Clock: return m_runtimeConfig.viewClockEnabled;
        case ViewMode::AlternativeClock: return m_runtimeConfig.viewAlternativeClockEnabled;
        case ViewMode::DayClock: return m_runtimeConfig.viewDayClockEnabled;
        case ViewMode::Options: return m_runtimeConfig.viewOptionsEnabled;
        case ViewMode::Image: return m_runtimeConfig.viewImageEnabled;
        case ViewMode::PomodoroControl: return m_runtimeConfig.viewImageEnabled;
        case ViewMode::PomodoroSettings: return m_runtimeConfig.viewImageEnabled;
    }
    return false;
}

int RadarApp::findEnabledRowInColumn(int column, int startRow) const
{
    const uint8_t rowCount = rowCountForColumn(column);
    if (rowCount == 0) {
        return -1;
    }

    int row = startRow % rowCount;
    if (row < 0) {
        row += rowCount;
    }

    for (uint8_t i = 0; i < rowCount; ++i) {
        if (isViewEnabled(viewForColumnRow(column, row))) {
            return row;
        }
        row = (row + 1) % rowCount;
    }
    return -1;
}

bool RadarApp::selectFirstEnabledView()
{
    for (int column = 0; column < kColumnCount; ++column) {
        const int row = findEnabledRowInColumn(column, m_columnRow[column]);
        if (row >= 0) {
            m_currentColumn = static_cast<uint8_t>(column);
            m_columnRow[column] = static_cast<uint8_t>(row);
            m_viewMode = viewForColumnRow(column, row);
            return true;
        }
    }

    m_currentColumn = 0;
    m_columnRow[0] = 0;
    m_viewMode = ViewMode::Radar;
    return false;
}

String RadarApp::currentViewName() const
{
    switch (m_viewMode) {
        case ViewMode::Radar: return "Radar";
        case ViewMode::Clock: return "Clock";
        case ViewMode::Sky: return "Sky";
        case ViewMode::Watch: return "Watch";
        case ViewMode::Options: return "Options";
        case ViewMode::AlternativeClock: return "Alt Clock";
        case ViewMode::DayClock: return "Day Clock";
        case ViewMode::Image: return "Image";
        case ViewMode::PomodoroControl: return "Pomodoro";
        case ViewMode::PomodoroSettings: return "Pomodoro Set";
    }
    return "Radar";
}

String RadarApp::currentThemeName() const
{
    return String(radarThemeName(m_runtimeConfig.radarTheme));
}
