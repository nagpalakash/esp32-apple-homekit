@echo off
setlocal
REM Wrapper so you can run:  scripts\esp32 ports | flash deco | status | monitor
cd /d "%~dp0\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0esp32.ps1" %*
exit /b %ERRORLEVEL%
