@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not defined QT_ROOT set "QT_ROOT=C:\DOWNLOAD\Qt\6.11.1\mingw_64"
if not defined QT_TOOLS set "QT_TOOLS=C:\DOWNLOAD\Qt\Tools"
set "CMAKE_EXE=%QT_TOOLS%\CMake_64\bin\cmake.exe"
set "NINJA_EXE=%QT_TOOLS%\Ninja\ninja.exe"
set "MINGW_BIN=%QT_TOOLS%\mingw1310_64\bin"

if not exist "%QT_ROOT%\bin\windeployqt.exe" (
  echo [ERROR] Qt not found: %QT_ROOT%
  echo Set QT_ROOT to a Qt 6 MinGW installation.
  exit /b 1
)
if not exist "%CMAKE_EXE%" (
  echo [ERROR] CMake not found: %CMAKE_EXE%
  exit /b 1
)
if not exist "%NINJA_EXE%" (
  echo [ERROR] Ninja not found: %NINJA_EXE%
  exit /b 1
)

set "PATH=%MINGW_BIN%;%QT_ROOT%\bin;%PATH%"

echo [INFO] Cleaning previous build-win and dist outputs...
if exist build-win rmdir /s /q build-win
if exist dist rmdir /s /q dist

"%CMAKE_EXE%" -S . -B build-win -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%QT_ROOT%" ^
  -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
  -DBUILD_TESTING=OFF || exit /b 1

"%CMAKE_EXE%" --build build-win --parallel || exit /b 1

mkdir build-win\package\SereinScope
mkdir build-win\package\Components
xcopy /e /i /y build-win\bin\* build-win\package\SereinScope\ >nul || exit /b 1
xcopy /y build-win\component_packages\*.sscomponent build-win\package\Components\ >nul || exit /b 1
"%QT_ROOT%\bin\windeployqt.exe" --release --compiler-runtime ^
  --no-translations build-win\package\SereinScope\SereinScope.exe || exit /b 1
copy /y README.md build-win\package\SereinScope\README.md >nul

mkdir dist
powershell -NoProfile -Command ^
  "Compress-Archive -Path 'build-win\package\SereinScope','build-win\package\Components' -DestinationPath 'dist\SereinScope.zip' -Force" || exit /b 1
for /f "tokens=*" %%H in ('powershell -NoProfile -Command "(Get-FileHash 'dist\SereinScope.zip' -Algorithm SHA256).Hash"') do set "ZIP_HASH=%%H"
>dist\SereinScope.zip.sha256.txt echo %ZIP_HASH%  SereinScope.zip

rmdir /s /q build-win
echo.
echo [OK] dist\SereinScope.zip
echo [OK] dist\SereinScope.zip.sha256.txt
endlocal
