@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '%~dp0setup_qt_env.ps1' | Out-Host; & '%~dp0..\build.ps1' %*"
exit /b %errorlevel%
