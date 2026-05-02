@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-SRS-Modern-Patch.ps1"
if errorlevel 1 (
  echo.
  echo Install failed.
  pause
)
