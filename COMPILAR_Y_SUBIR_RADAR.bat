@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "PROJECT=%ROOT%radar2.0"
set "PY="
set "PORT_FILE=%TEMP%\radar_upload_port.txt"
set "LOG_FILE=%TEMP%\radar_platformio_upload_%RANDOM%.log"

echo ============================================
echo  RADAR - Compilar y subir firmware normal
echo ============================================
echo.

if not exist "%PROJECT%\platformio.ini" (
    echo ERROR: No encuentro el proyecto en:
    echo   %PROJECT%
    echo.
    pause
    exit /b 1
)

if defined RADAR_PYTHON call :try_python "%RADAR_PYTHON%"
if not defined PY call :try_python "py -3.14"
if not defined PY call :try_python "py -3"
if not defined PY call :try_python "python"

if not defined PY (
    echo ERROR: No encuentro ningun Python que pueda cargar PlatformIO.
    echo.
    echo Opciones admitidas:
    echo   - definir RADAR_PYTHON con la ruta a Python
    echo   - tener disponible "py" o "python" en PATH
    echo.
    pause
    exit /b 1
)

cd /d "%PROJECT%"
if errorlevel 1 (
    echo ERROR: No puedo entrar en %PROJECT%
    pause
    exit /b 1
)

set "UPLOAD_PORT="
if not "%~1"=="" (
    set "UPLOAD_PORT=%~1"
) else (
    echo Detectando puerto...
    %PY% "%PROJECT%\tools\select_upload_port.py" > "%PORT_FILE%"
    if errorlevel 1 (
        echo.
        echo ERROR: No he podido detectar puerto.
        echo Conecta la placa por UART. Normalmente debe aparecer como COM5 CH343.
        echo Tambien puedes forzarlo asi:
        echo   COMPILAR_Y_SUBIR_RADAR.bat COM5
        echo.
        if exist "%PORT_FILE%" del "%PORT_FILE%" >nul 2>&1
        pause
        exit /b 1
    )
    for /f "usebackq delims=" %%P in ("%PORT_FILE%") do set "UPLOAD_PORT=%%P"
    del "%PORT_FILE%" >nul 2>&1
)

if not defined UPLOAD_PORT (
    echo ERROR: Puerto vacio.
    pause
    exit /b 1
)

echo.
echo Python/PlatformIO: %PY%
echo Puerto seleccionado: !UPLOAD_PORT!
echo Entorno: release
echo.
echo Compilando y subiendo...
echo.

%PY% -m platformio run -e release -t upload --upload-port "!UPLOAD_PORT!" > "!LOG_FILE!" 2>&1
set "RESULT=%ERRORLEVEL%"
type "!LOG_FILE!"

echo.
if not "%RESULT%"=="0" (
    set "UPLOAD_REACHED_LEAVING="
    set "UPLOAD_RESET_STEP="
    findstr /C:"Leaving..." "!LOG_FILE!" >nul 2>&1
    if "!ERRORLEVEL!"=="0" set "UPLOAD_REACHED_LEAVING=1"
    findstr /C:"Hard resetting" "!LOG_FILE!" >nul 2>&1
    if "!ERRORLEVEL!"=="0" set "UPLOAD_RESET_STEP=1"

    if defined UPLOAD_REACHED_LEAVING if defined UPLOAD_RESET_STEP (
        echo ============================================
        echo  AVISO: firmware escrito, fallo el reset serie
        echo ============================================
        echo.
        echo El binario ya llego a la placa. El fallo es de pySerial
        echo al reabrir/configurar el puerto despues del reset.
        echo.
        echo Si la pantalla no arranca sola:
        echo   1. Pulsa RESET en la placa, o desconecta y reconecta USB.
        echo   2. Para subida mas limpia, usa el UART cuando aparezca COM5:
        echo      COMPILAR_Y_SUBIR_RADAR.bat COM5
        echo.
        del "!LOG_FILE!" >nul 2>&1
        pause
        exit /b 0
    )

    echo ============================================
    echo  ERROR: compilacion o subida fallida
    echo ============================================
    echo.
    echo Si estaba por USB nativo y fallo al resetear, prueba por UART:
    echo   COMPILAR_Y_SUBIR_RADAR.bat COM5
    echo.
    del "!LOG_FILE!" >nul 2>&1
    pause
    exit /b %RESULT%
)

del "!LOG_FILE!" >nul 2>&1
echo ============================================
echo  OK: firmware normal subido correctamente
echo ============================================
echo Puerto: !UPLOAD_PORT!
echo.
pause
exit /b 0

:try_python
set "PY_CANDIDATE=%~1"
%PY_CANDIDATE% -c "import platformio" >nul 2>&1
if "%ERRORLEVEL%"=="0" set "PY=%PY_CANDIDATE%"
exit /b 0
