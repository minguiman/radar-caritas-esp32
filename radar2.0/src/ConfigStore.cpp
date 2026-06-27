#include "ConfigStore.h"

#include "SharedSd.h"

#include <ArduinoJson.h>
#include <Preferences.h>

using namespace esp_panel::board;

namespace
{
constexpr const char* kConfigDir = "/config";
constexpr const char* kConfigPath = "/config/radar.json";
constexpr const char* kConfigTmpPath = "/config/radar.tmp";
constexpr const char* kConfigBakPath = "/config/radar.bak";
constexpr const char* kStatePath = "/config/state.json";
constexpr const char* kStateTmpPath = "/config/state.tmp";
constexpr const char* kStateBakPath = "/config/state.bak";

void applyJsonToConfig(JsonDocument& doc, RuntimeConfig& config)
{
    if (doc["wifi"].is<JsonArray>()) {
        JsonArray wifi = doc["wifi"].as<JsonArray>();
        for (int i = 0; i < AppConfig::kMaxWifiProfiles; ++i) {
            JsonObject profile = wifi[i];
            config.wifiSsid[i] = profile["ssid"] | "";
            config.wifiPassword[i] = profile["pass"] | "";
        }
    } else {
        config.wifiSsid[0] = doc["wifiSsid"] | config.wifiSsid[0];
        config.wifiPassword[0] = doc["wifiPassword"] | config.wifiPassword[0];
    }

    config.locationLabel = doc["locationLabel"] | config.locationLabel;
    config.ownshipLat = doc["lat"] | config.ownshipLat;
    config.ownshipLon = doc["lon"] | config.ownshipLon;
    config.rangeKm = doc["rangeKm"] | config.rangeKm;
    config.radarTheme = doc["radarTheme"] | config.radarTheme;
    config.northBeepEnabled = doc["northBeep"] | config.northBeepEnabled;
    config.militaryOnlyEnabled = doc["militaryOnly"] | config.militaryOnlyEnabled;
    config.brightness = doc["brightness"] | config.brightness;
    config.apiRefreshSeconds = doc["apiRefresh"] | config.apiRefreshSeconds;
    config.fps = doc["fps"] | config.fps;
    config.gyroFps = doc["gyroFps"] | config.gyroFps;
    config.idleDimEnabled = doc["idleDim"] | config.idleDimEnabled;

    JsonObject alarm = doc["alarm"];
    if (!alarm.isNull()) {
        config.alarmEnabled = alarm["enabled"] | config.alarmEnabled;
        config.alarmHour = alarm["hour"] | config.alarmHour;
        config.alarmMinute = alarm["minute"] | config.alarmMinute;
    }

    JsonObject views = doc["views"];
    if (!views.isNull()) {
        config.viewRadarEnabled = views["radar"] | config.viewRadarEnabled;
        config.viewSkyEnabled = views["sky"] | config.viewSkyEnabled;
        config.viewWatchEnabled = views["watch"] | config.viewWatchEnabled;
        config.viewClockEnabled = views["clock"] | config.viewClockEnabled;
        config.viewAlternativeClockEnabled = views["altClock"] | config.viewAlternativeClockEnabled;
        config.viewDayClockEnabled = views["dayClock"] | config.viewDayClockEnabled;
        config.viewOptionsEnabled = views["options"] | config.viewOptionsEnabled;
        config.viewImageEnabled = views["image"] | config.viewImageEnabled;
    }

    JsonObject pomodoro = doc["pomodoro"];
    if (!pomodoro.isNull()) {
        config.pomodoroFocusMinutes = pomodoro["focus"] | config.pomodoroFocusMinutes;
        config.pomodoroBreakMinutes = pomodoro["break"] | config.pomodoroBreakMinutes;
        config.pomodoroLongBreakMinutes = pomodoro["longBreak"] | config.pomodoroLongBreakMinutes;
        config.waterReminderMinutes = pomodoro["water"] | config.waterReminderMinutes;
        config.stretchReminderMinutes = pomodoro["stretch"] | config.stretchReminderMinutes;
        config.pomodoroSoundEnabled = pomodoro["sound"] | config.pomodoroSoundEnabled;
    }
}

void writeConfigToJson(const RuntimeConfig& config, JsonDocument& doc)
{
    doc["version"] = 2;
    doc["configured"] = true;

    JsonArray wifi = doc.createNestedArray("wifi");
    for (int i = 0; i < AppConfig::kMaxWifiProfiles; ++i) {
        JsonObject profile = wifi.createNestedObject();
        profile["ssid"] = config.wifiSsid[i];
        profile["pass"] = config.wifiPassword[i];
    }

    doc["locationLabel"] = config.locationLabel;
    doc["lat"] = config.ownshipLat;
    doc["lon"] = config.ownshipLon;
    doc["rangeKm"] = config.rangeKm;
    doc["radarTheme"] = config.radarTheme;
    doc["northBeep"] = config.northBeepEnabled;
    doc["militaryOnly"] = config.militaryOnlyEnabled;
    doc["brightness"] = config.brightness;
    doc["apiRefresh"] = config.apiRefreshSeconds;
    doc["fps"] = config.fps;
    doc["gyroFps"] = config.gyroFps;
    doc["idleDim"] = config.idleDimEnabled;

    JsonObject alarm = doc.createNestedObject("alarm");
    alarm["enabled"] = config.alarmEnabled;
    alarm["hour"] = config.alarmHour;
    alarm["minute"] = config.alarmMinute;

    JsonObject views = doc.createNestedObject("views");
    views["radar"] = config.viewRadarEnabled;
    views["sky"] = config.viewSkyEnabled;
    views["watch"] = config.viewWatchEnabled;
    views["clock"] = config.viewClockEnabled;
    views["altClock"] = config.viewAlternativeClockEnabled;
    views["dayClock"] = config.viewDayClockEnabled;
    views["options"] = config.viewOptionsEnabled;
    views["image"] = config.viewImageEnabled;

    JsonObject pomodoro = doc.createNestedObject("pomodoro");
    pomodoro["focus"] = config.pomodoroFocusMinutes;
    pomodoro["break"] = config.pomodoroBreakMinutes;
    pomodoro["longBreak"] = config.pomodoroLongBreakMinutes;
    pomodoro["water"] = config.waterReminderMinutes;
    pomodoro["stretch"] = config.stretchReminderMinutes;
    pomodoro["sound"] = config.pomodoroSoundEnabled;
}
}

void ConfigStore::begin(Board& board)
{
    m_board = &board;
    SharedSd::setBoard(board);
}

bool ConfigStore::load(RuntimeConfig& config) const
{
    if (loadFromSd(config)) {
        return true;
    }

    if (!loadFromNvs(config)) {
        return false;
    }

    // Migration path: first boot after SD config support copies old NVS values to SD.
    saveToSd(config);
    return true;
}

bool ConfigStore::save(const RuntimeConfig& config) const
{
    return saveToSd(config);
}

bool ConfigStore::saveAlarm(bool enabled, uint8_t hour, uint8_t minute) const
{
    RuntimeConfig config;
    if (!load(config)) {
        return false;
    }
    config.alarmEnabled = enabled;
    config.alarmHour = hour;
    config.alarmMinute = minute;
    config.normalize();
    return save(config);
}

int ConfigStore::loadLastWifiProfileIndex() const
{
    int sdIndex = 0;
    if (loadLastWifiProfileIndexFromSd(sdIndex)) {
        return sdIndex;
    }

    Preferences prefs;
    if (!prefs.begin(AppConfig::kConfigNamespace, true)) {
        return 0;
    }

    const int index = static_cast<int>(prefs.getUChar("wifi_last", 0));
    prefs.end();
    if (index < 0 || index >= AppConfig::kMaxWifiProfiles) {
        return 0;
    }
    return index;
}

bool ConfigStore::saveLastWifiProfileIndex(int profileIndex) const
{
    if (profileIndex < 0 || profileIndex >= AppConfig::kMaxWifiProfiles) {
        return false;
    }

    if (saveLastWifiProfileIndexToSd(profileIndex)) {
        return true;
    }

    Preferences prefs;
    if (!prefs.begin(AppConfig::kConfigNamespace, false)) {
        return false;
    }

    prefs.putUChar("wifi_last", static_cast<uint8_t>(profileIndex));
    prefs.end();
    return true;
}

void ConfigStore::clear() const
{
    if (m_board != nullptr) {
        SharedSd::setBoard(*m_board);
    }
    if (SharedSd::ensureMounted()) {
        auto& sd = SharedSd::fs();
        sd.remove(kConfigPath);
        sd.remove(kConfigTmpPath);
        sd.remove(kConfigBakPath);
        sd.remove(kStatePath);
        sd.remove(kStateTmpPath);
        sd.remove(kStateBakPath);
    }

    Preferences prefs;
    if (prefs.begin(AppConfig::kConfigNamespace, false)) {
        prefs.clear();
        prefs.end();
    }
}

bool ConfigStore::loadFromSd(RuntimeConfig& config) const
{
    if (m_board != nullptr) {
        SharedSd::setBoard(*m_board);
    }
    if (!SharedSd::ensureMounted()) {
        return false;
    }
    auto& sd = SharedSd::fs();

    File file = sd.open(kConfigPath, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        return false;
    }

    DynamicJsonDocument doc(4096);
    const DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err || !(doc["configured"] | false)) {
        return false;
    }

    applyJsonToConfig(doc, config);
    config.normalize();
    return config.isValid();
}

bool ConfigStore::saveToSd(const RuntimeConfig& config) const
{
    RuntimeConfig normalizedConfig = config;
    normalizedConfig.normalize();

    if (m_board != nullptr) {
        SharedSd::setBoard(*m_board);
    }
    if (!SharedSd::ensureMounted()) {
        return false;
    }
    auto& sd = SharedSd::fs();

    if (!sd.exists(kConfigDir)) {
        sd.mkdir(kConfigDir);
    }

    DynamicJsonDocument doc(4096);
    writeConfigToJson(normalizedConfig, doc);

    String payload;
    payload.reserve(2048);
    serializeJson(doc, payload);
    if (payload.isEmpty()) {
        return false;
    }

    sd.remove(kConfigTmpPath);
    File file = sd.open(kConfigTmpPath, FILE_WRITE);
    if (!file) {
        return false;
    }

    const size_t written = file.print(payload);
    file.close();
    if (written != payload.length()) {
        sd.remove(kConfigTmpPath);
        return false;
    }

    sd.remove(kConfigBakPath);
    const bool hadPrevious = sd.exists(kConfigPath);
    if (hadPrevious && !sd.rename(kConfigPath, kConfigBakPath)) {
        sd.remove(kConfigTmpPath);
        return false;
    }
    if (!sd.rename(kConfigTmpPath, kConfigPath)) {
        if (hadPrevious) {
            sd.rename(kConfigBakPath, kConfigPath);
        }
        sd.remove(kConfigTmpPath);
        return false;
    }
    sd.remove(kConfigBakPath);
    return true;
}

bool ConfigStore::loadFromNvs(RuntimeConfig& config) const
{
    Preferences prefs;
    if (!prefs.begin(AppConfig::kConfigNamespace, false)) {
        return false;
    }

    const bool configured = prefs.getBool("configured", false);
    if (!configured) {
        prefs.end();
        return false;
    }

    if (prefs.isKey("wifi_ssid0")) {
        for (int i = 0; i < AppConfig::kMaxWifiProfiles; ++i) {
            const String ssidKey = String("wifi_ssid") + String(i);
            const String passKey = String("wifi_pass") + String(i);
            config.wifiSsid[i] = prefs.getString(ssidKey.c_str(), "");
            config.wifiPassword[i] = prefs.getString(passKey.c_str(), "");
        }
    } else {
        config.wifiSsid[0] = prefs.getString("wifi_ssid", AppConfig::kDefaultWifiSsid);
        config.wifiPassword[0] = prefs.getString("wifi_pass", AppConfig::kDefaultWifiPassword);
    }
    config.locationLabel = prefs.getString("loc_label", AppConfig::kLocationLabel);

    if (prefs.isKey("lat_num")) {
        config.ownshipLat = prefs.getDouble("lat_num", AppConfig::kOwnshipLat);
    } else {
        config.ownshipLat = prefs.getString("lat", String(AppConfig::kOwnshipLat, 6)).toDouble();
    }

    if (prefs.isKey("lon_num")) {
        config.ownshipLon = prefs.getDouble("lon_num", AppConfig::kOwnshipLon);
    } else {
        config.ownshipLon = prefs.getString("lon", String(AppConfig::kOwnshipLon, 6)).toDouble();
    }
    config.alarmEnabled = prefs.getBool("alarm_on", AppConfig::kDefaultAlarmEnabled);
    config.alarmHour = static_cast<uint8_t>(prefs.getUChar("alarm_hour", AppConfig::kDefaultAlarmHour));
    config.alarmMinute = static_cast<uint8_t>(prefs.getUChar("alarm_min", AppConfig::kDefaultAlarmMinute));
    config.rangeKm = prefs.getFloat("range_km", AppConfig::kRangeKm);
    config.radarTheme = static_cast<uint8_t>(prefs.getUChar("rad_theme", AppConfig::kDefaultRadarTheme));
    config.northBeepEnabled = prefs.getBool("north_beep", AppConfig::kDefaultNorthBeepEnabled);
    config.militaryOnlyEnabled = prefs.getBool("mil_only", false);
    config.brightness = static_cast<uint8_t>(prefs.getUChar("brightness", AppConfig::kDefaultBrightness));
    config.apiRefreshSeconds = static_cast<uint8_t>(prefs.getUChar("api_refresh", AppConfig::kDefaultApiRefreshSeconds));
    config.fps = static_cast<uint8_t>(prefs.getUChar("fps", AppConfig::kDefaultFps));
    config.gyroFps = static_cast<uint8_t>(prefs.getUChar("gyro_fps", AppConfig::kDefaultGyroFps));
    if (!prefs.getBool("gyro10_mig", false)) {
        if (config.gyroFps == AppConfig::kDefaultFps) {
            config.gyroFps = AppConfig::kDefaultGyroFps;
            prefs.putUChar("gyro_fps", config.gyroFps);
        }
        prefs.putBool("gyro10_mig", true);
    }
    config.viewRadarEnabled = prefs.getBool("v_radar", true);
    config.viewSkyEnabled = prefs.getBool("v_sky", true);
    config.viewWatchEnabled = prefs.getBool("v_watch", false);
    config.viewClockEnabled = prefs.getBool("v_clock", false);
    config.viewAlternativeClockEnabled = prefs.getBool("v_altclk", false);
    config.viewDayClockEnabled = prefs.getBool("v_dayclk", true);
    config.viewOptionsEnabled = prefs.getBool("v_opts", true);
    config.viewImageEnabled = prefs.getBool("v_image", false);
    config.idleDimEnabled = prefs.getBool("idle_dim", true);
    config.pomodoroFocusMinutes = static_cast<uint8_t>(prefs.getUChar("pomo_focus", 25));
    config.pomodoroBreakMinutes = static_cast<uint8_t>(prefs.getUChar("pomo_break", 5));
    config.pomodoroLongBreakMinutes = static_cast<uint8_t>(prefs.getUChar("pomo_long", 20));
    config.waterReminderMinutes = static_cast<uint8_t>(prefs.getUChar("rem_water", 60));
    config.stretchReminderMinutes = static_cast<uint8_t>(prefs.getUChar("rem_stretch", 20));
    config.pomodoroSoundEnabled = prefs.getBool("pomo_sound", true);

    prefs.end();

    config.normalize();
    return config.isValid();
}

bool ConfigStore::saveToNvs(const RuntimeConfig& config) const
{
    RuntimeConfig normalizedConfig = config;
    normalizedConfig.normalize();

    Preferences prefs;
    if (!prefs.begin(AppConfig::kConfigNamespace, false)) {
        return false;
    }

    prefs.putBool("configured", true);
    for (int i = 0; i < AppConfig::kMaxWifiProfiles; ++i) {
        const String ssidKey = String("wifi_ssid") + String(i);
        const String passKey = String("wifi_pass") + String(i);
        prefs.putString(ssidKey.c_str(), normalizedConfig.wifiSsid[i]);
        prefs.putString(passKey.c_str(), normalizedConfig.wifiPassword[i]);
    }
    prefs.putString("wifi_ssid", normalizedConfig.wifiSsid[0]);
    prefs.putString("wifi_pass", normalizedConfig.wifiPassword[0]);
    prefs.putString("loc_label", normalizedConfig.locationLabel);
    prefs.putDouble("lat_num", normalizedConfig.ownshipLat);
    prefs.putDouble("lon_num", normalizedConfig.ownshipLon);
    prefs.putBool("alarm_on", normalizedConfig.alarmEnabled);
    prefs.putUChar("alarm_hour", normalizedConfig.alarmHour);
    prefs.putUChar("alarm_min", normalizedConfig.alarmMinute);
    prefs.putFloat("range_km", normalizedConfig.rangeKm);
    prefs.putUChar("rad_theme", normalizedConfig.radarTheme);
    prefs.putBool("north_beep", normalizedConfig.northBeepEnabled);
    prefs.putBool("mil_only", normalizedConfig.militaryOnlyEnabled);
    prefs.putUChar("brightness", normalizedConfig.brightness);
    prefs.putUChar("api_refresh", normalizedConfig.apiRefreshSeconds);
    prefs.putUChar("fps", normalizedConfig.fps);
    prefs.putUChar("gyro_fps", normalizedConfig.gyroFps);
    prefs.putBool("v_radar", normalizedConfig.viewRadarEnabled);
    prefs.putBool("v_sky", normalizedConfig.viewSkyEnabled);
    prefs.putBool("v_watch", normalizedConfig.viewWatchEnabled);
    prefs.putBool("v_clock", normalizedConfig.viewClockEnabled);
    prefs.putBool("v_altclk", normalizedConfig.viewAlternativeClockEnabled);
    prefs.putBool("v_dayclk", normalizedConfig.viewDayClockEnabled);
    prefs.putBool("v_opts", normalizedConfig.viewOptionsEnabled);
    prefs.putBool("v_image", normalizedConfig.viewImageEnabled);
    prefs.putBool("idle_dim", normalizedConfig.idleDimEnabled);
    prefs.putUChar("pomo_focus", normalizedConfig.pomodoroFocusMinutes);
    prefs.putUChar("pomo_break", normalizedConfig.pomodoroBreakMinutes);
    prefs.putUChar("pomo_long", normalizedConfig.pomodoroLongBreakMinutes);
    prefs.putUChar("rem_water", normalizedConfig.waterReminderMinutes);
    prefs.putUChar("rem_stretch", normalizedConfig.stretchReminderMinutes);
    prefs.putBool("pomo_sound", normalizedConfig.pomodoroSoundEnabled);
    prefs.end();
    return true;
}

bool ConfigStore::loadLastWifiProfileIndexFromSd(int& profileIndex) const
{
    if (m_board != nullptr) {
        SharedSd::setBoard(*m_board);
    }
    if (!SharedSd::ensureMounted()) {
        return false;
    }
    auto& sd = SharedSd::fs();

    File file = sd.open(kStatePath, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        return false;
    }

    DynamicJsonDocument doc(256);
    const DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        return false;
    }

    const int index = doc["lastWifiProfile"] | 0;
    if (index < 0 || index >= AppConfig::kMaxWifiProfiles) {
        return false;
    }
    profileIndex = index;
    return true;
}

bool ConfigStore::saveLastWifiProfileIndexToSd(int profileIndex) const
{
    if (m_board != nullptr) {
        SharedSd::setBoard(*m_board);
    }
    if (!SharedSd::ensureMounted()) {
        return false;
    }
    auto& sd = SharedSd::fs();

    if (!sd.exists(kConfigDir)) {
        sd.mkdir(kConfigDir);
    }

    DynamicJsonDocument doc(256);
    doc["lastWifiProfile"] = profileIndex;

    String payload;
    payload.reserve(64);
    serializeJson(doc, payload);
    if (payload.isEmpty()) {
        return false;
    }

    sd.remove(kStateTmpPath);
    File file = sd.open(kStateTmpPath, FILE_WRITE);
    if (!file) {
        return false;
    }

    const size_t written = file.print(payload);
    file.close();
    if (written != payload.length()) {
        sd.remove(kStateTmpPath);
        return false;
    }

    sd.remove(kStateBakPath);
    const bool hadPrevious = sd.exists(kStatePath);
    if (hadPrevious && !sd.rename(kStatePath, kStateBakPath)) {
        sd.remove(kStateTmpPath);
        return false;
    }
    if (!sd.rename(kStateTmpPath, kStatePath)) {
        if (hadPrevious) {
            sd.rename(kStateBakPath, kStatePath);
        }
        sd.remove(kStateTmpPath);
        return false;
    }
    sd.remove(kStateBakPath);
    return true;
}
