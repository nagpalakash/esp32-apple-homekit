@echo off
REM Quick flash Deco Light
cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0esp32.ps1" flash deco %*
