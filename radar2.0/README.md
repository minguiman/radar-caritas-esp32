# Radar ESP32-S3 Waveshare 2.1

Firmware for the **Waveshare ESP32-S3 Touch LCD 2.1** round display board with a `480x480` screen.

## Project showcase

<p align="center">
  <img src="../docs/images/showcase/radar-air-traffic.png" alt="Air traffic radar mode" width="32%">
  <img src="../docs/images/showcase/clock-home-screen.png" alt="Clock home screen" width="32%">
  <img src="../docs/images/showcase/sky-map-mode.png" alt="Sky map mode" width="32%">
</p>
<p align="center">
  <img src="../docs/images/showcase/pomodoro-main.png" alt="Pomodoro timer mode" width="32%">
  <img src="../docs/images/showcase/system-options.png" alt="System options screen" width="32%">
  <img src="../docs/images/showcase/training-radar.png" alt="Training radar mini-game" width="32%">
</p>

## Target hardware

- `ESP32-S3R8`
- `ST7701` display driver
- `CST820` capacitive touch controller
- `16MB` flash
- `8MB` PSRAM
- `QMI8658` IMU
- `PCF85063` RTC
- `TF / microSD` slot
- `3.7V` LiPo charging support

## Board overview

According to the official Waveshare documentation, the `ESP32-S3-Touch-LCD-2.1` is an HMI-oriented development board built around a round `480x480` touch display with `2.4 GHz` Wi-Fi, `BLE 5`, `16 MB` flash, `8 MB` PSRAM, a 6-axis IMU, RTC, buzzer, microSD, and battery support.

- CPU: `ESP32-S3R8`, dual-core Xtensa LX7 up to `240 MHz`
- Display: `2.1"` capacitive touch panel, `480x480`, `262K` colors
- Connectivity: `Wi-Fi 802.11 b/g/n` + `Bluetooth LE 5`
- Peripherals: `QMI8658`, `PCF85063`, buzzer, LiPo charger, microSD
- Expansion: `UART`, `I2C`, USB-C for power/programming, and external antenna `IPEX1`
- Approximate board size: `75 x 75 mm`

## Hardware reference images

![Waveshare ESP32-S3-Touch-LCD-2.1 board](https://docs.waveshare.com/assets/images/ESP32-S3-Touch-LCD-2.1-product2-d6ea79598973f23e58797ebf82d3f56b.webp)

![Waveshare ESP32-S3-Touch-LCD-2.1 onboard resources](https://docs.waveshare.com/assets/images/ESP32-S3-Touch-LCD-2.1-product3-5f3a60f01d37c961cc26b890c4eddfc4.webp)

![Waveshare ESP32-S3-Touch-LCD-2.1 dimensions](https://docs.waveshare.com/assets/images/ESP32-S3-Touch-LCD-2.1-introduction-03-f07ca3d6969a1598ca0dce5833dff89b.webp)

## Official references

- [Waveshare ESP32-S3-Touch-LCD-2.1 documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1)
- [Waveshare product page](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm)

## Main components

- `src/RadarApp.*`: app flow, modes, Wi-Fi setup, and config portal
- `src/RadarRenderer.*`: rendering and UI drawing
- `src/RadarModel.*`: sweep state and radar model logic
- `src/ConfigStore.*`: persistent settings in NVS / SD
- `src/TrafficClient.*`: traffic, weather, and network requests
- `platformio.ini`: build configuration

## Public defaults included in this repo

This repository does **not** include personal Wi-Fi credentials.

Current public defaults:

```cpp
inline constexpr char kDefaultWifiSsid[] = "";
inline constexpr char kDefaultWifiPassword[] = "";
inline constexpr char kSetupApSsid[] = "Radar-Setup";
inline constexpr char kSetupApPassword[] = "";
inline constexpr char kLocationLabel[] = "Madrid";
inline constexpr double kOwnshipLat = 40.4168;
inline constexpr double kOwnshipLon = -3.7038;
```

The initial setup AP uses a chip-derived SSID in the form `Radar-Setup-XXXXXX`. The setup password is intentionally left empty in the public repo so no embedded credential is shipped here.

## First boot flow

1. Power on the board.
2. If no saved configuration exists, the firmware starts a setup access point.
3. The screen shows the AP SSID and portal IP.
4. Connect to that network from a phone or PC.
5. Open the displayed IP, usually `192.168.4.1`.
6. Enter your own Wi-Fi credentials and location.
7. Save the configuration and reboot.

## Build

From `radar2.0/`:

```powershell
py -3 -m platformio run -e release
```

## Flash firmware

You can use PlatformIO directly or the root script `..\COMPILAR_Y_SUBIR_RADAR.bat`.

Direct example:

```powershell
py -3 -m platformio run -e release -t upload --upload-port COM5
```
