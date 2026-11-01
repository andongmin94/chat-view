@echo off
setlocal DisableDelayedExpansion
rem Leave batch context before PowerShell removes this launcher.
(goto) 2>nul & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1" -Companion -CompanionPath "%~dp0." & if errorlevel 1 (pause & exit /b 1) else exit /b 0
