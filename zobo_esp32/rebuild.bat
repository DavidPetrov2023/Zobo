@echo off
echo ========================================
echo Zobo ESP32 - Clean Build and Flash
echo ========================================

cd /d "%~dp0"

echo.
echo [1/3] Cleaning build files...
if exist build rmdir /s /q build
if exist sdkconfig del /f sdkconfig
echo Done.

echo.
echo [2/3] Building firmware...
call idf.py build
if %errorlevel% neq 0 (
    echo BUILD FAILED!
    pause
    exit /b 1
)

echo.
echo [3/3] Flashing to ESP32...
set /p PORT="Enter COM port (default COM9): "
if "%PORT%"=="" set PORT=COM9

call idf.py -p %PORT% flash monitor

pause
