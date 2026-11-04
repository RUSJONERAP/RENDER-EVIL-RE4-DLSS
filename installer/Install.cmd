@echo off
setlocal
set "RENDER_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "RENDER_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%RENDER_PS%" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Setup.ps1"
if errorlevel 1 pause
