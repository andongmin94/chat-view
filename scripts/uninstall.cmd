@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1"
set "CHATVIEW_EXIT=%ERRORLEVEL%"
if not "%CHATVIEW_EXIT%"=="0" pause
endlocal & exit /b %CHATVIEW_EXIT%
