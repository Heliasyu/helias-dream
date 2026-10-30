@echo off
rem Starts the Helia's Dream engine without the launcher and writes its log to
rem the logs folder (the newest ten are kept).
rem   run-engine.bat            the per-user config (%APPDATA%\HeliasDream\HeliasDream.toml)
rem   run-engine.bat --setup    the setup window first
cd /d "%~dp0"
if not exist logs mkdir logs
powershell -NoProfile -Command "Get-ChildItem logs\HeliasDream-*.log | Sort-Object LastWriteTime -Descending | Select-Object -Skip 9 | Remove-Item" 2>nul
set TS=
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set TS=%%i
if "%TS%"=="" set TS=latest
set LOG=logs\HeliasDream-%TS%.log
echo.
echo  Helia's Dream is starting. Keep this window open while you play.
echo  Log file: %CD%\%LOG%
echo  (send that file when you report a problem)
echo.
HeliasDream-engine.exe %* > "%LOG%" 2>&1
set RC=%ERRORLEVEL%
echo.
echo  Helia's Dream ended (exit code %RC%). The last lines of the log:
echo.
powershell -NoProfile -Command "Get-Content -Tail 15 '%LOG%'"
echo.
pause
