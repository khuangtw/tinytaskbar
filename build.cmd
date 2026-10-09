@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 exit /b 1
if not exist out mkdir out
python tools\write_source_identity.py
if errorlevel 1 exit /b 1
set "TT_CXX=/nologo /std:c++17 /EHsc /MT /Os /W4 /WX /permissive- /DUNICODE /D_UNICODE /Isrc /Iout /Itests /Foout\"
set "TT_LINK=/DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF"
cl %TT_CXX% tests\broker_tests.cpp /link /OUT:out\BrokerTests.exe %TT_LINK%
if errorlevel 1 exit /b 1
cl %TT_CXX% tests\protocol_tests.cpp /link /OUT:out\ProtocolTests.exe %TT_LINK%
if errorlevel 1 exit /b 1
cl %TT_CXX% tests\mutex_tests.cpp /link /OUT:out\MutexTests.exe %TT_LINK%
if errorlevel 1 exit /b 1
cl %TT_CXX% /LD src\tap.cpp /link /OUT:out\TinyTaskbarTap-v4.dll /DEF:src\tap.def /IMPLIB:out\tap.lib %TT_LINK% user32.lib advapi32.lib ole32.lib oleaut32.lib runtimeobject.lib windowsapp.lib
if errorlevel 1 exit /b 1
cl %TT_CXX% tools\write_dll_identity.cpp /link /OUT:out\WriteDllIdentity.exe %TT_LINK% bcrypt.lib
if errorlevel 1 exit /b 1
out\WriteDllIdentity.exe out\TinyTaskbarTap-v4.dll out\dll_build_identity.hpp
if errorlevel 1 exit /b 1
cl %TT_CXX% src\manual.cpp src\startup.cpp /link /OUT:out\TinyTaskbar.exe /SUBSYSTEM:WINDOWS %TT_LINK% user32.lib shell32.lib ole32.lib uuid.lib advapi32.lib psapi.lib bcrypt.lib
if errorlevel 1 exit /b 1
echo Built native x64 v4 controller, broker and offline checks in out. Read README before running.
exit /b 0
