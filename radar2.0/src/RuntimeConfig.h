#pragma once

#include "AppConfig.h"

#include <Arduino.h>
#include <cmath>

struct RuntimeConfig
{
    String wifiSsid[AppConfig::kMaxWifiProfiles] = {
        AppConfig::kDefaultWifiSsid, "", ""
    };
    String wifiPassword[AppConfig::kMaxWifiProfiles] = {
        AppConfig::kDefaultWifiPassword, "", ""
    };
    String locationLabel = AppConfig::kLocationLabel;
    double ownshipLat = AppConfig::kOwnshipLat;
    double ownshipLon = AppConfig::kOwnshipLon;
    float rangeKm = AppConfig::kRangeKm;
    uint8_t radarTheme = AppConfig::kDefaultRadarTheme;
    bool alarmEnabled = AppConfig::kDefaultAlarmEnabled;
    uint8_t alarmHour = AppConfig::kDefaultAlarmHour;
    uint8_t alarmMinute = AppConfig::kDefaultAlarmMinute;
    bool northBeepEnabled = AppConfig::kDefaultNorthBeepEnabled;
    bool militaryOnlyEnabled = false;
    uint8_t brightness = AppConfig::kDefaultBrightness;
    uint8_t apiRefreshSeconds = AppConfig::kDefaultApiRefreshSeconds;
    uint8_t fps = AppConfig::kDefaultFps;
    uint8_t gyroFps = AppConfig::kDefaultGyroFps;
    bool viewRadarEnabled = true;
    bool viewSkyEnabled = true;
    bool viewWatchEnabled = false;
    bool viewClockEnabled = false;
    bool viewAlternativeClockEnabled = false;
    bool viewDayClockEnabled = true;
    bool viewOptionsEnabled = true;
    bool viewImageEnabled = false;
    bool idleDimEnabled = true;
    uint8_t pomodoroFocusMinutes = 25;
    uint8_t pomodoroBreakMinutes = 5;
    uint8_t pomodoroLongBreakMinutes = 20;
    uint8_t waterReminderMinutes = 60;
    uint8_t stretchReminderMinutes = 20;
    bool pomodoroSoundEnabled = true;

    bool hasWiFi() const
    {
        for (int i = 0; i < AppConfig::kMaxWifiProfiles; ++i) {
            if (!wifiSsid[i].isEmpty()) {
                return true;
            }
        }
        return false;
    }

    bool hasValidLocation() const
    {
        return isfinite(ownshipLat) && isfinite(ownshipLon)
            && ownshipLat >= -90.0 && ownshipLat <= 90.0
            && ownshipLon >= -180.0 && ownshipLon <= 180.0;
    }

    bool isValid() const
    {
        return hasWiFi() && hasValidLocation() && rangeKm > 0.0f;
    }

    void normalize()
    {
        for (int i = 0; i < AppConfig::kMaxWifiProfiles; ++i) {
            wifiSsid[i].trim();
            if (wifiSsid[i].isEmpty()) {
                wifiPassword[i] = "";
            }
        }

        locationLabel.trim();
        if (locationLabel.isEmpty()) {
            locationLabel = AppConfig::kLocationLabel;
        }

        if (!isfinite(ownshipLat) || ownshipLat < -90.0 || ownshipLat > 90.0) {
            ownshipLat = AppConfig::kOwnshipLat;
        }
        if (!isfinite(ownshipLon) || ownshipLon < -180.0 || ownshipLon > 180.0) {
            ownshipLon = AppConfig::kOwnshipLon;
        }

        if (!isfinite(rangeKm) || rangeKm <= 0.0f) {
            rangeKm = AppConfig::kRangeKm;
        }
        if (rangeKm < AppConfig::kMinRangeKm) {
            rangeKm = AppConfig::kMinRangeKm;
        }
        if (rangeKm > AppConfig::kMaxRangeKm) {
            rangeKm = AppConfig::kMaxRangeKm;
        }
        const float steps = roundf((rangeKm - AppConfig::kMinRangeKm) / AppConfig::kRangeOptionStepKm);
        rangeKm = AppConfig::kMinRangeKm + (steps * AppConfig::kRangeOptionStepKm);
        if (rangeKm < AppConfig::kMinRangeKm) {
            rangeKm = AppConfig::kMinRangeKm;
        }
        if (rangeKm > AppConfig::kMaxRangeKm) {
            rangeKm = AppConfig::kMaxRangeKm;
        }

        if (radarTheme > AppConfig::kRadarThemeDragonBall) {
            radarTheme = AppConfig::kDefaultRadarTheme;
        }
        if (alarmHour > 23) {
            alarmHour = AppConfig::kDefaultAlarmHour;
        }
        if (alarmMinute > 59) {
            alarmMinute = AppConfig::kDefaultAlarmMinute;
        }
        alarmMinute = static_cast<uint8_t>((alarmMinute / 5) * 5);
        if (brightness < AppConfig::kMinBrightness) {
            brightness = AppConfig::kMinBrightness;
        }
        if (brightness > AppConfig::kMaxBrightness) {
            brightness = AppConfig::kMaxBrightness;
        }
        if (apiRefreshSeconds < AppConfig::kMinApiRefreshSeconds) {
            apiRefreshSeconds = AppConfig::kMinApiRefreshSeconds;
        }
        if (apiRefreshSeconds > AppConfig::kMaxApiRefreshSeconds) {
            apiRefreshSeconds = AppConfig::kMaxApiRefreshSeconds;
        }
        const uint8_t refreshSteps = static_cast<uint8_t>(
            roundf(static_cast<float>(apiRefreshSeconds - AppConfig::kMinApiRefreshSeconds)
                / static_cast<float>(AppConfig::kApiRefreshStepSeconds)));
        apiRefreshSeconds = AppConfig::kMinApiRefreshSeconds
            + (refreshSteps * AppConfig::kApiRefreshStepSeconds);
        if (apiRefreshSeconds < AppConfig::kMinApiRefreshSeconds) {
            apiRefreshSeconds = AppConfig::kMinApiRefreshSeconds;
        }
        if (apiRefreshSeconds > AppConfig::kMaxApiRefreshSeconds) {
            apiRefreshSeconds = AppConfig::kMaxApiRefreshSeconds;
        }

        fps = normalizeFps(fps);
        gyroFps = normalizeGyroFps(gyroFps);

        if (!viewRadarEnabled && !viewSkyEnabled && !viewWatchEnabled && !viewClockEnabled
            && !viewAlternativeClockEnabled && !viewDayClockEnabled && !viewOptionsEnabled
            && !viewImageEnabled) {
            viewRadarEnabled = true;
        }

        pomodoroFocusMinutes = normalizeChoice(pomodoroFocusMinutes, 25, 25, 30, 45, 50);
        pomodoroBreakMinutes = normalizeChoice(pomodoroBreakMinutes, 5, 5, 10, 15, 15);
        pomodoroLongBreakMinutes = normalizeChoice(pomodoroLongBreakMinutes, 20, 15, 20, 25, 30, 30);
        waterReminderMinutes = normalizeChoice(waterReminderMinutes, 60, 0, 10, 15, 30, 45, 60, 90);
        stretchReminderMinutes = normalizeChoice(stretchReminderMinutes, 20, 0, 10, 15, 20, 30, 45, 60);
    }

    static uint8_t normalizeFps(uint8_t value)
    {
        if (value < AppConfig::kMinFps) {
            value = AppConfig::kMinFps;
        }
        if (value > AppConfig::kMaxFps) {
            value = AppConfig::kMaxFps;
        }
        const uint8_t fpsSteps = static_cast<uint8_t>(
            roundf(static_cast<float>(value - AppConfig::kMinFps) / static_cast<float>(AppConfig::kFpsStep)));
        return AppConfig::kMinFps + (fpsSteps * AppConfig::kFpsStep);
    }

    static uint8_t normalizeGyroFps(uint8_t value)
    {
        constexpr uint8_t options[] = {7, 10, 15, 20, 25, 30, 35};
        uint8_t best = options[0];
        uint8_t bestDistance = value > best ? value - best : best - value;
        for (uint8_t option : options) {
            const uint8_t distance = value > option ? value - option : option - value;
            if (distance < bestDistance) {
                best = option;
                bestDistance = distance;
            }
        }
        return best;
    }

    static uint8_t normalizeChoice(uint8_t value, uint8_t fallback,
                                   uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e = 255)
    {
        const uint8_t options[] = {a, b, c, d, e};
        for (uint8_t option : options) {
            if (option != 255 && value == option) {
                return value;
            }
        }
        return fallback;
    }

    static uint8_t normalizeChoice(uint8_t value, uint8_t fallback,
                                   uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                                   uint8_t e, uint8_t f, uint8_t g)
    {
        const uint8_t options[] = {a, b, c, d, e, f, g};
        for (uint8_t option : options) {
            if (option != 255 && value == option) {
                return value;
            }
        }
        return fallback;
    }
};
