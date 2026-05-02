@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-SRS-Modern-Patch.ps1"
if errorlevel 1 (
  echo.
  echo Uninstall failed.
  pause
)
