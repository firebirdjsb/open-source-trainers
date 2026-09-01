@echo off
setlocal
where msbuild >nul 2>nul
if errorlevel 1 (
  echo ERROR: msbuild was not found. Open "Developer Command Prompt for VS 2022" and run this file there.
  pause
  exit /b 1
)
msbuild "%~dp0IncursionCheat_DX12.sln" /m /t:Build /p:Configuration=Release /p:Platform=x64
if errorlevel 1 (
  echo BUILD FAILED.
  pause
  exit /b 1
)
echo.
echo Build complete:
echo   %~dp0bin\x64\Release\IncursionCheat_DX12.dll
echo   %~dp0bin\x64\Release\Loader.exe
pause
