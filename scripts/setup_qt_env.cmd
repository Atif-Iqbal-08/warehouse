@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup_qt_env.ps1" %*
exit /b %errorlevel%
