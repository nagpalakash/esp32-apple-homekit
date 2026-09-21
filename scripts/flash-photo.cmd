@echo off
REM Quick flash Photo Light
cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0esp32.ps1" flash photo %*
