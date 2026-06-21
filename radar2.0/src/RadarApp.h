#pragma once

#include "BoardImu.h"
#include "BoardRtc.h"
#include "ConfigStore.h"
#include "RadarModel.h"
#include "RadarRenderer.h"
#include "RuntimeConfig.h"
#include "TrafficClient.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <esp_display_panel.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <array>
#include <time.h>

class RadarApp
{
public:
    explicit RadarApp(esp_panel::board::Board* board);
    ~RadarApp();

    bool begin();
    void loop();

    void handleRootRequest();
    void handleSaveRequest();

private:
    enum class Mode
    {
        SetupPortal,
        Radar
    };

    enum class ViewMode
    {
        Radar,
        Clock,
        Sky,
        Watch,
        Options,
        AlternativeClock,
        DayClock,
        Image,
        PomodoroControl,
        PomodoroSettings
    };

    enum class BuzzerCue : uint8_t
    {
        None,
        PomodoroStart,
        PomodoroPause,
        PomodoroReset,
        WaterReminder,
        EyesReminder,
        FocusComplete,
        BreakComplete
    };

    static void networkTaskEntry(void* context);
    static void handleRootRequestThunk();
    static void handleSaveRequestThunk();
    void networkTask();
    bool scheduleNetworkFetch(uint32_t nowMs, uint32_t nextDelayMs, bool requestTraffic = true, bool requestWeather = false);
    void connectWiFiBlocking();
    bool connectWiFiProfile(int profileIndex, uint32_t timeoutMs);
    bool wifiStationReady() const;
    bool restoreTimeFromRtc();
    bool syncNetworkTimeIfDue(bool force);
    bool syncRtcFromSystemTime();
    bool systemTimeValid() const;
    void applyStationPowerSaving();
    void suspendWiFiForClock();
    void resumeWiFiAfterClock();
    void updateClockImu(uint32_t nowMs);
    bool startSetupPortal();
    bool startRadarMode();
    void stopRadarMode();
    void stopSetupPortal();
    void enterSetupPortal();
    void exitSetupPortal();
    void applyRuntimeConfigToSystems();
    void persistRuntimeConfig();
    void requestRuntimeConfigSave(uint32_t nowMs);
    void updateRuntimeConfigSave(uint32_t nowMs);
    void updateConfigSaveModal(uint32_t nowMs);
    bool isRuntimeConfigEditView(ViewMode view) const;
    bool saveRuntimeConfigNow();
    void renderCurrentMode();
    void flushDisplay();
    void handleTouch();
    static ViewMode viewForColumnRow(int column, int row);
    static uint8_t rowCountForColumn(int column);
    void changeColumnStep(int delta);
    void toggleColumnRow();
    void changeColumnRowStep(int delta);
    void applyViewModeChange(ViewMode previous);
    void showFaceView();
    void handleOptionsShortTap(int x, int y);
    void handlePomodoroControlTap(int x, int y);
    void handlePomodoroSettingsTap(int x, int y);
    void startOrResumePomodoro(uint32_t nowMs);
    void pausePomodoro(uint32_t nowMs);
    void togglePomodoro(uint32_t nowMs);
    void resetPomodoro();
    void updatePomodoro(uint32_t nowMs);
    void cyclePomodoroFocus();
    void cyclePomodoroBreak();
    void cyclePomodoroLongBreak();
    void cyclePomodoroSound();
    void cycleWaterReminder();
    void cycleStretchReminder();
    uint32_t pomodoroPhaseDurationMs() const;
    String pomodoroTimeText() const;
    String pomodoroStateText() const;
    String reminderIntervalText(uint8_t minutes) const;
    void changeRangeStep(int delta);
    void changeApiRefreshStep(int delta);
    void changeFpsStep(int delta);
    void changeGyroFpsStep(int delta);
    void changeRadarThemeStep(int delta);
    void setAlarmEnabled(bool enabled);
    void setNorthBeepEnabled(bool enabled);
    void cycleAlarmHourStep(int delta);
    void cycleAlarmMinuteStep(int delta);
    void applyBacklightBrightness(uint8_t brightness);
    void registerUserActivity(uint32_t nowMs);
    bool restoreBrightnessAfterIdle(uint32_t nowMs);
    void updateIdleBrightness(uint32_t nowMs);
    void setBrightness(uint8_t brightness);
    void cycleBrightnessStep(int delta);
    void updateClockAlarm();
    void updateNorthBeep(uint32_t nowMs, bool radarView);
    void stopNorthBeep();
    void startDragonBallTouchBeep(uint32_t nowMs);
    void updateDragonBallTouchBeep(uint32_t nowMs);
    void stopDragonBallTouchBeep();
    void startClockAlarm(int alarmKey);
    void dismissClockAlarm();
    void playBuzzerCue(BuzzerCue cue, uint32_t nowMs);
    void updateBuzzerCue(uint32_t nowMs);
    uint8_t buzzerCueStepCount(BuzzerCue cue) const;
    uint16_t buzzerCueStepMs(BuzzerCue cue, uint8_t stepIndex) const;
    void initAlarmOutput();
    void setAlarmOutput(bool on);
    void syncBuzzerOutput();
    void setRadarStatus(const String& statusText);
    bool tryOpenPlanePopup(int x, int y);
    void closePlanePopup();
    bool isViewEnabled(ViewMode view) const;
    int findEnabledRowInColumn(int column, int startRow) const;
    bool selectFirstEnabledView();
    String currentViewName() const;
    String currentThemeName() const;
    String htmlEscape(const String& value) const;
    uint32_t apiRefreshMs() const;
    bool detectRadarNorthCrossing(float currentSweepAngleDeg);
    uint32_t millisUntilNextNorth(float currentSweepAngleDeg) const;
    void prepareRadarWiFiForNorth(uint32_t nowMs);
    void setRadarWiFiActiveForSnapshot(bool active);
    void requestRadarSnapshotAtNorth(uint32_t nowMs);

    esp_panel::board::Board* m_board = nullptr;
    esp_panel::drivers::LCD* m_lcd = nullptr;
    esp_panel::drivers::Touch* m_touch = nullptr;
    lv_color_t* m_lcdBuffer[2] = {};
    lv_color_t* m_partialUploadBuffer = nullptr;
    bool m_usePanelFrameBuffers = false;
    int m_renderBufferIndex = 0;
    RadarModel m_model;
    RadarRenderer m_renderer;
    BoardImu m_imu;
    BoardRtc m_rtc;
    TrafficClient m_trafficClient;
    SemaphoreHandle_t m_fetchMutex = nullptr;
    TaskHandle_t m_networkTaskHandle = nullptr;
    FetchResult m_pendingFetchResult;
    WeatherResult m_pendingWeatherResult;
    WeatherResult m_dayClockWeather;
    ConfigStore m_configStore;
    RuntimeConfig m_runtimeConfig;
    bool m_configSavePending = false;
    uint32_t m_configSaveRequestedMs = 0;
    uint32_t m_configSaveLastAttemptMs = 0;
    bool m_configSaveModalActive = false;
    bool m_configSaveModalAttempted = false;
    bool m_configSaveModalOk = false;
    uint32_t m_configSaveModalStartMs = 0;
    uint32_t m_configSaveModalUntilMs = 0;
    uint32_t m_configSaveModalLastRenderMs = 0;
    DNSServer m_dnsServer;
    WebServer m_webServer{80};
    Mode m_mode = Mode::SetupPortal;
    ViewMode m_viewMode = ViewMode::Radar;
    ViewMode m_resumeViewMode = ViewMode::Radar;
    // Navegación en rejilla: columna actual y última fila usada por columna (RAM).
    // Por defecto al arrancar: col0=Radar (fila 0), col1=Clock (fila 1), col2=Options
    // (fila 0). En la columna 1, fila 0 = Watch y fila 1 = Clock, así que su fila por
    // defecto es 1 para que el arranque muestre Clock.
    static constexpr int kColumnCount = 4;
    uint8_t m_currentColumn = 0;
    uint8_t m_columnRow[kColumnCount] = {0, 1, 0, 1};
    String m_portalStatusLine1;
    String m_portalStatusLine2;
    String m_apIpText;
    String m_apSsidText;
    volatile bool m_hasPendingFetch = false;
    volatile bool m_hasPendingWeather = false;
    volatile bool m_dayClockWeatherRequested = false;
    volatile bool m_networkRequestTraffic = false;
    volatile bool m_networkRequestWeather = false;
    bool m_restartRequested = false;
    uint32_t m_lastRenderMs = 0;
    uint32_t m_startupAnimationStartMs = 0;
    bool m_startupAnimationDone = false;
    int m_lastDayClockMinuteKey = -1;
    uint32_t m_nextDayClockWeatherFetchMs = 0;
    uint32_t m_lastUserActivityMs = 0;
    uint8_t m_appliedBrightness = 0;
    bool m_idleBrightnessDimmed = false;
    bool m_idleSleepActive = false;
    uint32_t m_lastClockImuUpdateMs = 0;
    uint32_t m_lastClockMotionMs = 0;
    uint32_t m_lastNtpAttemptMs = 0;
    uint32_t m_lastNtpSyncMs = 0;
    bool m_hasClockRenderPose = false;
    bool m_clockMotionActive = false;
    bool m_clockWifiSuspended = false;
    float m_lastClockRenderTiltX = 0.0f;
    float m_lastClockRenderTiltY = 0.0f;
    float m_lastClockRenderRotation = 0.0f;

    uint32_t m_touchPressMs = 0;
    int m_touchX = 0;
    int m_touchY = 0;
    int m_touchLastX = 0;
    int m_touchLastY = 0;
    bool m_touchWasPressed = false;
    bool m_touchConsumed = false;
    bool m_planePopupTapPending = false;
    uint32_t m_lastTouchPollMs = 0;
    uint32_t m_touchIgnoreUntilMs = 0;
    uint32_t m_lastLabelRefreshMs = 0;
    bool m_hasRadarSweepAngle = false;
    float m_lastRadarSweepAngleDeg = 0.0f;
    bool m_refreshRadarLabelsOnNextRender = true;
    uint8_t m_radarLabelNorthPassesSinceRefresh = 0;
    uint32_t m_lastRadarNorthSnapshotMs = 0;
    bool m_radarWifiAwakeForSnapshot = false;
    uint32_t m_lastRadarWifiPrewarmMs = 0;
    uint8_t m_radarLabelMode = AppConfig::kLabelModeFlightLevel;
    uint8_t m_skyLabelMode = 0;
    String m_radarStatusText;
    uint32_t m_radarStatusUntilMs = 0;
    volatile uint32_t m_nextApiFetchMs = 0;
    volatile bool m_fetchInProgress = false;
    uint8_t m_consecutiveFetchFailures = 0;
    bool m_trafficApiFailureVisible = false;
    bool m_hasDragonBallApiFetch = false;
    uint32_t m_lastDragonBallApiFetchMs = 0;
    bool m_alarmOutputReady = false;
    bool m_alarmRinging = false;
    bool m_alarmBeepOn = false;
    int m_lastAlarmKey = -1;
    uint32_t m_lastAlarmBeepMs = 0;
    uint32_t m_alarmStartMs = 0;
    BuzzerCue m_buzzerCue = BuzzerCue::None;
    bool m_buzzerCueOn = false;
    uint8_t m_buzzerCueStep = 0;
    uint32_t m_buzzerCueNextMs = 0;
    bool m_northBeepActive = false;
    bool m_hasNorthBeepSweepAngle = false;
    float m_lastNorthBeepSweepAngle = 0.0f;
    uint32_t m_northBeepEndMs = 0;
    bool m_dragonBallTouchBeepActive = false;
    bool m_dragonBallTouchBeepOn = false;
    uint32_t m_dragonBallTouchBeepStartMs = 0;
    bool m_dragonBallRenderWasActive = false;
    uint32_t m_lastWifiReconnectAttemptMs = 0;
    uint32_t m_wifiDisconnectedSinceMs = 0;
    uint32_t m_radarViewEnteredMs = 0;
    bool m_pomodoroStarted = false;
    bool m_pomodoroRunning = false;
    bool m_pomodoroBreakPhase = false;
    bool m_pomodoroFocusReady = false;
    uint8_t m_pomodoroFocusMinutes = 25;
    uint8_t m_pomodoroBreakMinutes = 5;
    uint8_t m_pomodoroLongBreakMinutes = 20;
    uint8_t m_waterReminderMinutes = 60;
    uint8_t m_stretchReminderMinutes = 20;
    bool m_pomodoroSoundEnabled = true;
    uint8_t m_pomodoroCompletedFocus = 0;
    uint32_t m_pomodoroRemainingMs = 25UL * 60UL * 1000UL;
    uint32_t m_pomodoroLastTickMs = 0;
    uint32_t m_lastPomodoroUpdateMs = 0;
    uint32_t m_nextWaterReminderMs = 0;
    uint32_t m_waterReminderUntilMs = 0;
    bool m_waterReminderVisible = false;
    uint32_t m_nextStretchReminderMs = 0;
    uint32_t m_stretchReminderUntilMs = 0;
    bool m_stretchReminderVisible = false;
    char m_wifiConnectStatus[44] = "";
    bool m_planePopupOpen = false;
    uint32_t m_planePopupOpenedMs = 0;
    Plane m_planePopupPlane;
    String m_lastDeselectedPlaneId;
    uint32_t m_lastDeselectedPlaneMarkerUntilMs = 0;
};
