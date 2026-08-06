@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if not exist "dist\SereinScope.zip" (
  echo Run build_win.bat first.
  pause
  exit /b 1
)
if exist "out\run-win" rmdir /s /q "out\run-win"
mkdir "out\run-win"
powershell -NoProfile -Command ^
  "Expand-Archive -Path 'dist\SereinScope.zip' -DestinationPath 'out\run-win' -Force" || exit /b 1
start "" "out\run-win\SereinScope\SereinScope.exe"
