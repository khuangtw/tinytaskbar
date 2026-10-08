@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
  echo Open an x64 Native Tools Command Prompt for Visual Studio first.
  exit /b 1
)
if not exist out mkdir out
if errorlevel 1 exit /b 1
set "TT_CXX=/nologo /std:c++17 /EHsc /MT /Os /W4 /WX /permissive- /DUNICODE /D_UNICODE /Isrc /Iout /Itests /Foout\"
set "TT_LINK=/DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF"
cl %TT_CXX% /LD src\tap.cpp /link /OUT:out\TinyTaskbarTap-v3.dll /DEF:src\tap.def /IMPLIB:out\tap.lib %TT_LINK% user32.lib ole32.lib oleaut32.lib runtimeobject.lib windowsapp.lib
if errorlevel 1 exit /b 1
cl %TT_CXX% tools\write_dll_identity.cpp /link /OUT:out\WriteDllIdentity.exe %TT_LINK% bcrypt.lib
if errorlevel 1 exit /b 1
out\WriteDllIdentity.exe out\TinyTaskbarTap-v3.dll out\dll_build_identity.hpp
if errorlevel 1 exit /b 1
cl %TT_CXX% src\manual.cpp src\startup.cpp /link /OUT:out\TinyTaskbar.exe /SUBSYSTEM:WINDOWS %TT_LINK% user32.lib shell32.lib ole32.lib uuid.lib advapi32.lib psapi.lib bcrypt.lib
if errorlevel 1 exit /b 1
cl %TT_CXX% tests\lifecycle_tests.cpp /link /OUT:out\LifecycleTests.exe %TT_LINK%
if errorlevel 1 exit /b 1
cl %TT_CXX% tests\regression_tests.cpp /link /OUT:out\RegressionTests.exe %TT_LINK% bcrypt.lib
if errorlevel 1 exit /b 1
cl %TT_CXX% tests\manual_filecheck.cpp /link /OUT:out\ManualFileChecks.exe %TT_LINK% bcrypt.lib
if errorlevel 1 exit /b 1
cl %TT_CXX% /DTINY_TASKBAR_STARTUP_TESTS tests\startup_tests.cpp src\startup.cpp /link /OUT:out\StartupTests.exe %TT_LINK% shell32.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
cl %TT_CXX% tests\session_safety_tests.cpp /link /OUT:out\SessionSafetyTests.exe %TT_LINK% ole32.lib oleaut32.lib runtimeobject.lib windowsapp.lib
if errorlevel 1 exit /b 1
echo Built the experimental x64 candidate and offline tests in out.
exit /b 0
