// SPDX-License-Identifier: GPL-3.0-only
// Private v4 controller. Native validation is still pending.
#include "dll_identity.hpp"
#include "controller_checks.hpp"
#include "startup.hpp"
#include "process_checks.hpp"
#include <shellapi.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <memory>

constexpr wchar_t WindowClass[]=L"TinyTaskbar.Controller.4eda63e0-v4";
constexpr UINT TrayMessage=WM_APP+1;
static HWND window=nullptr;
static UINT recreated=0;
static bool quitRequested=false,taskbarRecreated=false;
static HANDLE logFile=INVALID_HANDLE_VALUE;

static void pumpOwnMessages() {
    MSG message={};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
        if(message.message==WM_QUIT){quitRequested=true;break;}
        TranslateMessage(&message);DispatchMessageW(&message);
    }
}
static bool waitStartup(HANDLE signal,DWORD milliseconds) {
    ULONGLONG end=GetTickCount64()+milliseconds;
    for(;;) {
        pumpOwnMessages();if(quitRequested)return false;
        ULONGLONG now=GetTickCount64();if(now>=end)return false;
        DWORD result=MsgWaitForMultipleObjectsEx(1,&signal,DWORD(end-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if(result==WAIT_OBJECT_0){pumpOwnMessages();return !quitRequested;}
        if(result==WAIT_FAILED || result==WAIT_TIMEOUT)return false;
    }
}

static std::wstring ownPath() {
    wchar_t path[32768]={};DWORD n=GetModuleFileNameW(nullptr,path,32768);
    require(n && n<32768,"Cannot locate this executable.");return {path,n};
}
static void log(std::string const& message) noexcept {
    if(logFile==INVALID_HANDLE_VALUE)return;
    SYSTEMTIME t={};GetSystemTime(&t);char prefix[64]={};
    sprintf_s(prefix,"%04u-%02u-%02uT%02u:%02u:%02uZ ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    std::string line=std::string(prefix)+message+"\r\n";DWORD n=0;
    WriteFile(logFile,line.data(),DWORD(line.size()),&n,nullptr);
}
static std::string utf8(std::wstring const& s) {
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string result(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),result.data(),n,nullptr,nullptr);return result;
}
struct Log {
    explicit Log(std::wstring path) {
        path.resize(path.find_last_of(L"\\/")+1);path+=L"TinyTaskbar.log";
        logFile=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(logFile!=INVALID_HANDLE_VALUE,"Cannot write TinyTaskbar.log beside the executable.");
    }
    ~Log(){if(logFile!=INVALID_HANDLE_VALUE){CloseHandle(logFile);logFile=INVALID_HANDLE_VALUE;}}
};
static void nonElevated() {
    OwnedHandle token;require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value)!=FALSE,"Cannot read caller token.");
    TOKEN_ELEVATION elevation={};DWORD bytes=0;
    require(GetTokenInformation(token.value,TokenElevation,&elevation,sizeof(elevation),&bytes) && !elevation.TokenIsElevated,"Run TinyTaskbar without administrator privileges.");
}
static DWORD validate(HWND& bar,OwnedHandle& explorer) {
    bar=FindWindowW(L"Shell_TrayWnd",nullptr);require(bar!=nullptr,"Windows taskbar is unavailable; try again after Explorer starts.");
    DWORD pid=0,tid=GetWindowThreadProcessId(bar,&pid);require(pid && tid,"Taskbar owner is invalid.");
    explorer.value=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid);
    require(explorer.value!=nullptr,"Cannot query this taskbar's Explorer.");
    wchar_t image[32768]={},directory[MAX_PATH]={};DWORD n=32768;
    require(QueryFullProcessImageNameW(explorer.value,0,image,&n)!=FALSE,"Cannot read Explorer image path.");
    require(GetWindowsDirectoryW(directory,MAX_PATH)>0 && _wcsicmp(image,(std::wstring(directory)+L"\\explorer.exe").c_str())==0,"Taskbar owner is not Windows Explorer.");
    sameUserSession(explorer.value);BOOL wow=FALSE;
    require(IsWow64Process(explorer.value,&wow) && !wow,"Taskbar architecture differs from this x64 utility.");
    char line[160]={};sprintf_s(line,"Validated Explorer pid=%lu tid=%lu hwnd=%p same-user/session non-elevated",pid,tid,bar);log(line);return tid;
}
struct DllSelection {std::wstring path;bool reconnect=false;};
static DllSelection selectDll(HANDLE explorer,std::wstring executable) {
    std::vector<HMODULE> modules(512);DWORD bytes=0;
    require(EnumProcessModulesEx(explorer,modules.data(),DWORD(modules.size()*sizeof(HMODULE)),&bytes,LIST_MODULES_64BIT)!=FALSE,"Cannot inspect this Explorer's loaded DLLs.");
    if(bytes>modules.size()*sizeof(HMODULE)) {
        modules.resize(bytes/sizeof(HMODULE));
        require(EnumProcessModulesEx(explorer,modules.data(),DWORD(modules.size()*sizeof(HMODULE)),&bytes,LIST_MODULES_64BIT) && bytes<=modules.size()*sizeof(HMODULE),"Explorer's module list changed; try again.");
    }
    modules.resize(bytes/sizeof(HMODULE));std::wstring loaded;
    for(auto module:modules) {
        wchar_t name[MAX_PATH]={};if(!GetModuleBaseNameW(explorer,module,name,MAX_PATH))continue;
        auto classification=classifyDll(name);if(classification==DllGate::Unrelated)continue;
        require(classification==DllGate::Current,"An older/different TinyTaskbar or TranslucentTB diagnostics DLL is loaded. Wait for Explorer to exit naturally; no additional version will be loaded.");
        require(loaded.empty(),"Multiple TinyTaskbar DLL images are loaded; no additional image will be loaded.");
        wchar_t path[32768]={};DWORD n=GetModuleFileNameExW(explorer,module,path,32768);
        require(n && n<32768,"Cannot locate the already loaded TinyTaskbar DLL.");loaded.assign(path,n);
    }
    bool reconnect=!loaded.empty();
    if(!reconnect){executable.resize(executable.find_last_of(L"\\/")+1);loaded=executable+L"TinyTaskbarTap-v4.dll";}
    log(std::string(reconnect?"Reconnect to loaded broker DLL: ":"First initialization with adjacent DLL: ")+utf8(loaded));
    return {loaded,reconnect};
}
#include "controller_runtime.hpp"
static NOTIFYICONDATAW trayData() {
    NOTIFYICONDATAW data={};data.cbSize=sizeof(data);data.hWnd=window;data.uID=1;
    data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=TrayMessage;
    data.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(data.szTip,L"Taskbar transparency - quit to restore");return data;
}
struct Tray {
    bool visible=false;
    void add(){auto data=trayData();if(visible && Shell_NotifyIconW(NIM_MODIFY,&data))return;
        visible=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;require(visible,"Cannot create the Quit tray icon; restoring.");}
    ~Tray(){if(visible){auto data=trayData();Shell_NotifyIconW(NIM_DELETE,&data);}}
};
static void showMenu() {
    HMENU menu=CreatePopupMenu();if(!menu)return;
    AppendMenuW(menu,MF_STRING,1,L"Quit and restore");POINT position={};GetCursorPos(&position);SetForegroundWindow(window);
    UINT action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,position.x,position.y,0,window,nullptr);
    DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);if(action==1)quitRequested=true;
}
static LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) noexcept {
    if(message==recreated){taskbarRecreated=true;return 0;}
    if(message==TrayMessage && (lp==WM_RBUTTONUP || lp==WM_LBUTTONUP)){showMenu();return 0;}
    if(message==WM_CLOSE || (message==WM_ENDSESSION && wp)){quitRequested=true;return 0;}
    if(message==WM_QUERYENDSESSION)return TRUE;
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
struct Window {~Window(){if(window){DestroyWindow(window);window=nullptr;}}};
static int stopCompanion(std::wstring const& executable) {
    HWND controller=FindWindowW(WindowClass,nullptr);if(!controller){log("No manual controller is running.");return 0;}
    DWORD pid=0;GetWindowThreadProcessId(controller,&pid);OwnedHandle process;
    process.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);require(process.value!=nullptr,"Cannot query our manual controller.");
    sameUserSession(process.value);wchar_t path[32768]={};DWORD n=32768;
    require(QueryFullProcessImageNameW(process.value,0,path,&n) && _wcsicmp(path,executable.c_str())==0,"Quit target is not this TinyTaskbar executable.");
    DWORD stillPid=0;GetWindowThreadProcessId(controller,&stillPid);
    require(stillPid==pid && WaitForSingleObject(process.value,0)==WAIT_TIMEOUT,"Our controller changed before the quit request.");
    require(PostMessageW(controller,WM_CLOSE,0,0)!=FALSE,"Cannot request our controller to quit.");
    require(WaitForSingleObject(process.value,30000)==WAIT_OBJECT_0,"Controller did not exit within 30 seconds; preserve the log.");
    DWORD result=1;require(GetExitCodeProcess(process.value,&result)!=FALSE,"Cannot read controller exit status.");
    log("--stop observed controller exit code="+std::to_string(result));return int(result);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    std::unique_ptr<Log> logfile;
    try {
        auto executable=ownPath();logfile=std::make_unique<Log>(executable);log("TinyTaskbar invocation");nonElevated();
        int argc=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&argc);require(args!=nullptr,"Cannot read command line.");
        struct Args {LPWSTR* value;~Args(){LocalFree(value);}} cleanup{args};
        if(argc==2 && wcscmp(args[1],L"--stop")==0)return stopCompanion(executable);
        if(argc==2) {
            auto action=startup::parse(args[1]);
            if(action!=startup::Action::None) {
                auto folder=startup::userFolder();
                if(action==startup::Action::Enable)startup::enable(folder,executable);
                if(action==startup::Action::Disable)startup::disable(folder);
                auto message=startup::describe(startup::inspect(folder,executable));log(message);
                // Console output when invoked from a shell; no extra UI/process
                // remains running after a startup configuration command.
                if(AttachConsole(ATTACH_PARENT_PROCESS)) {
                    message+="\r\n";DWORD bytes=0;
                    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message.data(),DWORD(message.size()),&bytes,nullptr);
                    FreeConsole();
                }
                return 0;
            }
        }
        require(argc==1,"Usage: TinyTaskbar.exe [--stop | --startup-enable | --startup-disable | --startup-remove | --startup-status].");
        OwnedMutex controllerLock;claimMutex(controllerLock,L"Local\\TinyTaskbar.Controller.4eda63e0-v4");
        recreated=RegisterWindowMessageW(L"TaskbarCreated");require(recreated!=0,"Cannot register the taskbar notification.");
        WNDCLASSW type={};type.hInstance=instance;type.lpfnWndProc=windowProc;type.lpszClassName=WindowClass;
        require(RegisterClassW(&type)!=0,"Cannot register the controller window.");
        Window ownedWindow;window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,WindowClass,L"Taskbar transparency",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,instance,nullptr);
        require(window!=nullptr,"Cannot create the hidden controller window.");
        while(!quitRequested){
            Runtime runtime;runtime.start(executable);Tray tray;tray.add();bool shellEnded=false;
            while(!quitRequested){
                HANDLE handles[]={runtime.explorer.value,runtime.ipc.changed.value};
                DWORD result=MsgWaitForMultipleObjectsEx(2,handles,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
                require(result!=WAIT_FAILED,"Controller message wait failed.");
                if(result==WAIT_OBJECT_0){shellEnded=true;log("Explorer ended naturally; waiting up to 30 seconds for a new taskbar.");break;}
                pumpOwnMessages();
                require(!readLong(runtime.ipc.state->error) && readLong(runtime.ipc.state->status)!=StatusPoisoned,"Broker failed; restoring and stopping this controller.");
                if(taskbarRecreated){
                    taskbarRecreated=false;DWORD pid=0;GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd",nullptr),&pid);
                    if(pid && pid!=runtime.target){log("Taskbar owner changed before old Explorer ended; stopping safely.");quitRequested=true;break;}
                    tray.add();log("Same-Explorer taskbar recreated; broker retained, tray re-added.");
                }
            }
            runtime.ipc.stop();if(!shellEnded)break;
        }
        return 0;
    }catch(std::exception const& e){log(std::string("ERROR: ")+e.what());return 1;}
    catch(...){log("ERROR: unexpected controller failure.");return 1;}
}
