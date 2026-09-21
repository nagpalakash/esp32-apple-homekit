@echo off
REM HomeSpan status (pairing / IP / HomeKit clients)
cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0esp32.ps1" status %*
