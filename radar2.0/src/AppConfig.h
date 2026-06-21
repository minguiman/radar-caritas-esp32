#pragma once

#include <Arduino.h>

namespace AppConfig
{
inline constexpr uint16_t kScreenWidth = 480;
inline constexpr uint16_t kScreenHeight = 480;
inline constexpr uint32_t kSerialBaud = 2'000'000;
inline constexpr uint32_t kSdSpiHz = 20 * 1000 * 1000;
inline constexpr int kSdSpiSckPin = 2;
inline constexpr int kSdSpiMisoPin = 42;
inline constexpr int kSdSpiMosiPin = 1;
inline constexpr uint8_t kSdCsExpanderPin = 3;
inline constexpr uint8_t kLcdCsExpanderPin = 2;

inline constexpr char kLocationLabel[] = "Madrid";
inline constexpr double kOwnshipLat = 40.4168;
inline constexpr double kOwnshipLon = -3.7038;
inline constexpr float kRangeKm = 80.0f;
inline constexpr char kDefaultWifiSsid[] = "";
inline constexpr char kDefaultWifiPassword[] = "";
inline constexpr int kMaxWifiProfiles = 3;

inline constexpr char kSetupApSsid[] = "Radar-Setup";
inline constexpr char kSetupApSsidPrefix[] = "Radar-Setup-";
// Empty password keeps the setup AP free of embedded credentials.
inline constexpr char kSetupApPassword[] = "";
inline constexpr uint8_t kSetupApChannel = 1;
inline constexpr uint8_t kSetupApMaxClients = 4;
inline constexpr uint16_t kSetupDnsPort = 53;
inline constexpr char kConfigNamespace[] = "radarcfg";
inline constexpr char kTimezoneTz[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";

inline constexpr uint8_t kDefaultApiRefreshSeconds = 10;
inline constexpr uint8_t kMinApiRefreshSeconds = 10;
inline constexpr uint8_t kMaxApiRefreshSeconds = 60;
inline constexpr uint8_t kApiRefreshStepSeconds = 5;
inline constexpr uint32_t kApiRefreshMs = static_cast<uint32_t>(kDefaultApiRefreshSeconds) * 1000UL;
inline constexpr uint32_t kWiFiRetryMs = 3'000;
inline constexpr uint32_t kRadarWifiPrewarmMs = 3'000;
inline constexpr uint32_t kRadarWifiPowerSaveCooldownMs = 1'500;
inline constexpr uint32_t kContactHoldMs = 7'500;
inline constexpr uint32_t kContactFadeMs = 2'500;
inline constexpr uint32_t kLabelRotateMs = 2'000;
inline constexpr uint32_t kFrameIntervalMs = 40;
inline constexpr uint32_t kPortalRenderMs = 250;
inline constexpr uint32_t kTouchLongPressMs = 900;
inline constexpr uint32_t kTouchPollIntervalMs = 20;
inline constexpr uint32_t kImageFrameIntervalMs = 100;
inline constexpr uint32_t kRadarLoopMaxSleepMs = 12;
inline constexpr uint32_t kPopupLoopMaxSleepMs = 8;
inline constexpr uint32_t kRadarStatusOverlayMs = 1600;
// Buffer temporal para subidas parciales al LCD. Evita reservar otro framebuffer
// completo solo para copiar rectangulos pequeños. Si un rectangulo supera este
// tamaño, se fuerza full flush de forma segura.
inline constexpr uint16_t kPartialFlushBufferRows = 96;
inline constexpr size_t kPartialFlushMaxPixels = static_cast<size_t>(kScreenWidth) * kPartialFlushBufferRows;


inline constexpr uint8_t kLabelModeFlightLevel = 0;
inline constexpr uint8_t kLabelModeAircraftType = 1;
inline constexpr uint8_t kLabelModeHidden = 2;
inline constexpr uint8_t kLabelModeCount = 3;
inline constexpr bool kEnableRadarLabelModeTapCycle = false;
// Altura minima para crear/candidatar labels en el radar.
// Los contactos por debajo de este valor siguen pudiendo existir como blip,
// pero no entran en el calculo de etiquetas. 100 ft equivale a ~30 m.
// Si prefieres FL < 10, cambia este valor a 1000.0f.
inline constexpr float kMinRadarLabelAltitudeFeet = 100.0f;

inline constexpr int kMaxVisiblePlanes = 60;
inline constexpr int kMaxDisplayedLabelGroups = 16;
inline constexpr int kRadarMarginPx = 24;
inline constexpr int kRingStepKm = 10;
inline constexpr int kWideRangeRingStepKm = 20;
inline constexpr float kWideRangeRingStepThresholdKm = 100.0f;
inline constexpr int kLabelWidthPx = 94;
inline constexpr int kLabelHeightPx = 42;
inline constexpr int kLabelLineOffsetPx = 12;
inline constexpr int kPointSizePx = 4;
inline constexpr int kPlaneTouchRadiusPx = 34;
inline constexpr float kMinRangeKm = 10.0f;
inline constexpr float kMaxRangeKm = 200.0f;
inline constexpr float kRangeOptionStepKm = 10.0f;
inline constexpr uint8_t kMinBrightness = 10;
inline constexpr float kSweepAcquireWindowDeg = 5.0f;
// 25 en vez de 40: el ultimo tramo de la cola iba con alfa <0.04 (fade^2) y apenas
// se veia, pero pagaba ~40% de los blends de la cuña y agrandaba el rect sucio
// que se restaura/sube al panel cada frame.
inline constexpr float kSweepTailDeg = 40.0f;
inline constexpr int kRadarSweepTailLines = 12;
inline constexpr const char* kTrafficSourceAirplanesLive = "airplanes.live";
inline constexpr const char* kTrafficSourceName = "adsb.fi";
inline constexpr uint32_t kReadsbPlaneObjectBufferBytes = 2 * 1024;
inline constexpr uint32_t kReadsbPlaneJsonDocBytes = 3 * 1024;
inline constexpr uint32_t kReadsbJsonDocBytes = 160 * 1024;
inline constexpr uint32_t kMilJsonDocBytes = 256 * 1024;
inline constexpr uint32_t kAirplanesLiveMinIntervalMs = 1'000;
inline constexpr uint8_t kRadarThemeGreen = 0;
inline constexpr uint8_t kRadarThemeBlue = 1;
inline constexpr uint8_t kRadarThemeCyan = 2;
inline constexpr uint8_t kRadarThemeRed = 3;
inline constexpr uint8_t kRadarThemeDragonBall = 4;
inline constexpr uint8_t kDefaultRadarTheme = kRadarThemeGreen;
inline constexpr bool kDefaultAlarmEnabled = false;
inline constexpr uint8_t kDefaultAlarmHour = 7;
inline constexpr uint8_t kDefaultAlarmMinute = 0;
inline constexpr bool kDefaultNorthBeepEnabled = false;
inline constexpr uint8_t kBuzzerExpanderPin = 7;
inline constexpr uint32_t kAlarmBeepIntervalMs = 350;
inline constexpr uint32_t kAlarmAutoStopMs = 60'000;
inline constexpr uint32_t kNorthBeepPulseMs = 45;
inline constexpr uint32_t kDragonBallScanBeepPeriodMs = 1'500;
inline constexpr uint32_t kDragonBallScanFirstBeepMs = 35;
inline constexpr uint32_t kDragonBallScanSecondBeepMs = 70;
inline constexpr uint32_t kDragonBallScanBeepGapMs = 220;
inline constexpr uint32_t kDragonBallApiMinIntervalMs = 10'000;
inline constexpr uint8_t kDragonBallWaveCount = 3;
inline constexpr uint32_t kDragonBallWaveDurationMs = 3'000;
inline constexpr uint32_t kDragonBallHitGlowMs = 1'000;
inline constexpr uint8_t kDragonBallHitGlowCycles = 3;
inline constexpr uint32_t kDragonBallTotalWaveMs = static_cast<uint32_t>(kDragonBallWaveCount) * kDragonBallWaveDurationMs;
inline constexpr uint32_t kDragonBallTotalActiveMs = kDragonBallTotalWaveMs + kDragonBallHitGlowMs;
inline constexpr uint8_t kMaxBrightness = 100;
inline constexpr uint8_t kDefaultBrightness = 100;
inline constexpr uint8_t kMinFps = 15;
inline constexpr uint8_t kMaxFps = 35;
inline constexpr uint8_t kFpsStep = 5;
// 15 FPS por defecto para reducir carga y mantener el tactil mas reactivo.
// Sigue disponible subirlo en Opciones durante la sesion si se quiere mas suavidad.
inline constexpr uint8_t kDefaultFps = 15;
inline constexpr uint8_t kDefaultGyroFps = kDefaultFps;
inline constexpr bool kStartupAnimationEnabled = true;
inline constexpr uint32_t kStartupAnimationMs = 6'000;
inline constexpr uint8_t kStartupAnimationFps = kMaxFps;
}
