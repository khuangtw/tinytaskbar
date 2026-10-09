@echo off
setlocal
cd /d "%~dp0"
out\BrokerTests.exe
if errorlevel 1 exit /b 1
out\ProtocolTests.exe
if errorlevel 1 exit /b 1
out\MutexTests.exe
exit /b %errorlevel%
