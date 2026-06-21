# Radar ESP32-S3 Waveshare 2.1

Firmware para la placa **Waveshare ESP32-S3 Touch LCD 2.1** con pantalla redonda `480x480`.

## Hardware objetivo

- `ESP32-S3R8`
- LCD `ST7701`
- touch `CST820`
- `16MB` flash
- `8MB` PSRAM
- IMU `QMI8658`

## Componentes principales

- `src/RadarApp.*`: arranque, modos, WiFi y portal de configuracion
- `src/RadarRenderer.*`: render del radar y UI
- `src/RadarModel.*`: estado y sincronizacion del barrido
- `src/ConfigStore.*`: persistencia de configuracion en NVS
- `src/TrafficClient.*`: consultas a la API de trafico
- `platformio.ini`: configuracion de build

## Configuracion publica incluida

El repositorio **no** incluye credenciales WiFi personales.

Valores por defecto actuales:

```cpp
inline constexpr char kDefaultWifiSsid[] = "";
inline constexpr char kDefaultWifiPassword[] = "";
inline constexpr char kSetupApSsid[] = "Radar-Setup";
inline constexpr char kSetupApPassword[] = "";
inline constexpr char kLocationLabel[] = "Default";
inline constexpr double kOwnshipLat = 0.0;
inline constexpr double kOwnshipLon = 0.0;
```

El AP de configuracion inicial usa un SSID derivado del chip con formato `Radar-Setup-XXXXXX` y queda abierto por defecto para no publicar una contrasena embebida.

## Flujo de primer arranque

1. Enciende la placa.
2. Si no hay configuracion guardada, se crea un AP de configuracion.
3. En pantalla se muestran SSID e IP del portal.
4. Conectate a esa red desde movil o PC.
5. Abre la IP mostrada en pantalla, normalmente `192.168.4.1`.
6. Introduce tus propios SSID, passwords y ubicacion.
7. Guarda la configuracion y reinicia.

## Compilar

Desde `radar2.0/`:

```powershell
py -3 -m platformio run -e release
```

## Subir firmware

Puedes usar la CLI o el script raiz `..\COMPILAR_Y_SUBIR_RADAR.bat`.

Ejemplo directo:

```powershell
py -3 -m platformio run -e release -t upload --upload-port COM5
```

## Notas de publicacion

- Las rutas personales se eliminaron del repo.
- No se incluyen SSID/password reales.
- La ubicacion por defecto es neutra y debe sustituirse durante el setup.
