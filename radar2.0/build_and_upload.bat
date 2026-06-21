@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

echo ============================================
echo  radar2.0 - Compilar y subir a ESP32
echo ============================================
echo.

REM Si pasas un puerto como argumento (ej: build_and_upload.bat COM3), se usa directamente.
python "%~dp0tools\select_upload_port.py" %~1 > "%TEMP%\radar_upload_port.txt"
if errorlevel 1 (
    echo.
    echo Conecta la placa ESP32 por USB e intenta de nuevo.
    echo.
    pause
    exit /b 1
)

set "UPLOAD_PORT="
for /f "usebackq delims=" %%P in ("%TEMP%\radar_upload_port.txt") do set "UPLOAD_PORT=%%P"
del "%TEMP%\radar_upload_port.txt" >nul 2>&1

if not defined UPLOAD_PORT (
    echo ERROR: No se pudo determinar el puerto de subida.
    pause
    exit /b 1
)

echo.
echo Puerto seleccionado: !UPLOAD_PORT!
echo.
echo Compilando y subiendo firmware...
echo.

python -m platformio run -e release -t upload --upload-port !UPLOAD_PORT!
if !errorlevel! neq 0 (
    echo.
    echo ============================================
    echo  ERROR: Compilacion o subida fallida
    echo  Revisa los mensajes de error arriba
    echo ============================================
    pause
    exit /b !errorlevel!
)

echo.
echo ============================================
echo  Firmware compilado y subido correctamente
echo ============================================
echo.
echo Resumen:
echo  - Proyecto: radar2.0
echo  - Board: Waveshare ESP32-S3 Touch LCD 2.1
echo  - Puerto: !UPLOAD_PORT!
echo  - Estado: OK
echo.
pause
