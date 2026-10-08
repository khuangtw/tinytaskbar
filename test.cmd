@echo off
setlocal
cd /d "%~dp0"
out\LifecycleTests.exe
if errorlevel 1 exit /b 1
out\RegressionTests.exe
if errorlevel 1 exit /b 1
out\ManualFileChecks.exe out\TinyTaskbarTap-v3.dll
if errorlevel 1 exit /b 1
out\StartupTests.exe
if errorlevel 1 exit /b 1
out\SessionSafetyTests.exe
if errorlevel 1 exit /b 1
exit /b 0
