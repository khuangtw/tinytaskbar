// GPL-3.0. One reviewed GUI start/normal --stop trial. No DLL loader or hook.
#include "dll_identity.hpp"
#include "evidence.hpp"
#include "applied_wait.hpp"
#include "controller_checks.hpp"
#include <string>

struct Pattern {HWND value=nullptr;~Pattern(){if(value)DestroyWindow(value);}};
struct Child {
    std::wstring executable;
    OwnedHandle process,thread;
    bool stopRequested=false;
    explicit Child(std::wstring file):executable(std::move(file)) {
        std::wstring command=L"\""+executable+L"\"";STARTUPINFOW startup={};startup.cb=sizeof(startup);PROCESS_INFORMATION p={};
        require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&p)!=FALSE,"Cannot launch our reviewed manual controller.");
        process.value=p.hProcess;thread.value=p.hThread;
    }
    void stop() {
        if(stopRequested)return;stopRequested=true;
        std::wstring command=L"\""+executable+L"\" --stop";STARTUPINFOW startup={};startup.cb=sizeof(startup);PROCESS_INFORMATION p={};
        require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&p)!=FALSE,"Cannot launch our stop companion.");
        OwnedHandle companion,companionThread;companion.value=p.hProcess;companionThread.value=p.hThread;
        require(WaitForSingleObject(companion.value,35000)==WAIT_OBJECT_0,"Normal stop companion timed out; no forced kill.");
        DWORD result=1;require(GetExitCodeProcess(companion.value,&result) && result==0,"Stop companion reported unsuccessful cleanup.");
        require(WaitForSingleObject(process.value,1000)==WAIT_OBJECT_0,"Manual controller has not exited.");
        require(GetExitCodeProcess(process.value,&result) && result==0,"Manual controller exited with an error.");
    }
    ~Child(){if(process.value && !stopRequested && WaitForSingleObject(process.value,0)==WAIT_TIMEOUT)try{stop();}catch(std::exception const& e){fprintf(stderr,"Cleanup: %s\n",e.what());}}
};
int wmain() try {
    SetProcessDPIAware();HWND bar=FindWindowW(L"Shell_TrayWnd",nullptr);require(bar!=nullptr,"Taskbar not found.");
    DWORD target=0;GetWindowThreadProcessId(bar,&target);OwnedHandle explorer;
    explorer.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,target);require(explorer.value!=nullptr,"Cannot read this taskbar's Explorer metrics.");
    RECT original={};require(GetWindowRect(bar,&original) && original.right-original.left==2560 && original.bottom-original.top==72,"Inspected display dimensions changed.");
    require(!FindWindowW(L"TinyTaskbar.Manual.2cb3b83d-v3",nullptr) && !FindWindowW(L"TinyTaskbar.Manual.2cb3b83d-v2",nullptr),"A manual controller is already running; no trial start.");
    RECT region=original;region.top-=32;UINT recreated=RegisterWindowMessageW(L"TaskbarCreated");
    auto wait=[&](DWORD ms){require(pause(ms,recreated),"Taskbar changed during trial; normal cleanup requested.");};
    screenshot(original,L"00-stock.bmp");
    WNDCLASSW type={};type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=patternProc;type.lpszClassName=L"TinyTaskbar.ManualTrialPattern";
    require(RegisterClassW(&type)!=0,"Cannot register trial pattern.");Pattern pattern;
    pattern.value=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"Taskbar trial pattern",WS_POPUP,region.left,region.top,region.right-region.left,region.bottom-region.top,nullptr,nullptr,type.hInstance,nullptr);
    require(pattern.value!=nullptr,"Cannot create own pattern.");ShowWindow(pattern.value,SW_SHOWNOACTIVATE);UpdateWindow(pattern.value);wait(700);
    auto before=screenshot(region,L"01-before.bmp");auto baseline=measure(explorer.value);printMetrics("Explorer","before_load",baseline);
    wchar_t path[32768]={};DWORD length=GetModuleFileNameW(nullptr,path,32768);require(length && length<32768,"Cannot locate reviewed manual EXE.");
    std::wstring executable(path,length);executable.resize(executable.find_last_of(L"\\/")+1);executable+=L"TinyTaskbar.exe";
    Child child(executable);
    auto startup=waitApplied(child.process.value,30000,[&]{return OpenEventW(SYNCHRONIZE,FALSE,objectName(target,L"applied").c_str());});
    require(startup!=AppliedWait::ChildExited,"Manual controller exited before applied acknowledgment.");
    require(startup!=AppliedWait::Timeout,"Applied IPC event was not acknowledged before the startup deadline.");
    require(startup==AppliedWait::Applied,"Applied IPC event wait failed.");
    OwnedHandle mapping;mapping.value=OpenFileMappingW(FILE_MAP_READ,FALSE,objectName(target,L"state").c_str());require(mapping.value!=nullptr,"Active reviewed IPC was not found.");
    auto state=static_cast<State const*>(MapViewOfFile(mapping.value,FILE_MAP_READ,0,0,sizeof(State)));require(state!=nullptr,"Cannot read active IPC.");
    struct View {State const* state;~View(){UnmapViewOfFile(state);}} view{state};
    FILETIME c,e,k,u;require(GetProcessTimes(child.process.value,&c,&e,&k,&u)!=FALSE,"Cannot validate manual owner birth.");
    require(state->magic==ProtocolMagic && state->version==3 && state->ownerPid==GetProcessId(child.process.value) && state->ownerCreation==fileTicks(c) && state->targetPid==target && state->taskbar==UINT_PTR(bar) && state->maxLifetimeMs==0 && applyGate(*state,false,true)==ElementGate::Ready,"Manual lease/owner/live primary pair validation failed.");
    printf("actual_xaml BackgroundStroke Rectangle Taskbar.TaskbarFrame: fills=%ld strokes=%ld DIP rect=%.3f,%.3f,%.3f,%.3f raster_scale=%.3f\n",state->fills,state->strokes,state->strokeX,state->strokeY,state->strokeWidth,state->strokeHeight,state->rasterScale);
    printf("manual_ready target=%lu owner=%lu lease=0 applied=%ld hr=%08lx\n",target,state->ownerPid,state->applied,DWORD(state->error));
    wait(700);auto clear=screenshot(region,L"02-transparent.bmp");compare(before,clear,region.right-region.left,"before_vs_transparent");sharpStripes(clear,region.right-region.left);
    require(topEdgeMatch(clear,region.right-region.left,"after_clear")==8u*unsigned(region.right-region.left),"Top eight physical pixel rows still contain an edge effect.");
    std::vector<unsigned char>().swap(clear);auto startController=measure(child.process.value),startExplorer=measure(explorer.value);ULONGLONG start=GetTickCount64();
    printMetrics("controller","transparent_idle_start",startController);printMetrics("Explorer","transparent_idle_start",startExplorer);fflush(stdout);
    wait(30000);ULONGLONG elapsed=GetTickCount64()-start;auto endController=measure(child.process.value),endExplorer=measure(explorer.value);
    printMetrics("controller","transparent_idle_end",endController);printMetrics("Explorer","transparent_idle_end",endExplorer);
    printf("idle elapsed_ms=%llu controller_cpu_ms=%.4f whole_Explorer_cpu_ms=%.4f whole_Explorer_private_delta_from_baseline=%lld working_delta=%lld\n",elapsed,
        double(endController.cpu-startController.cpu)/10000,double(endExplorer.cpu-startExplorer.cpu)/10000,
        static_cast<long long>(endExplorer.privateBytes)-static_cast<long long>(baseline.privateBytes),static_cast<long long>(endExplorer.working)-static_cast<long long>(baseline.working));
    child.stop();
    printf("manual_restore backgrounds=%ld restored=%ld hr=%08lx cleanup_complete=%ld cleanup_retained=%ld\n",state->backgrounds,state->restored,DWORD(state->error),state->cleanupComplete,state->cleanupRetained);
    require(state->cleanupComplete==1 && state->cleanupRetained==0 && state->error==0,"Normal stop did not confirm cleanup.");
    require(state->restoredFills==state->fills && state->restoredStrokes==state->strokes,"Both original fill and border brushes must be restored.");
    wait(700);auto restored=screenshot(region,L"03-restored.bmp");compare(before,restored,region.right-region.left,"before_vs_restored");
    require(topEdgeRestore(before,restored,region.right-region.left)==0,"Top-edge original appearance was not restored.");
    DestroyWindow(pattern.value);pattern.value=nullptr;wait(700);screenshot(original,L"04-final.bmp");printMetrics("Explorer","after_restore",measure(explorer.value));
    printf("Complete: reviewed manual start and --stop succeeded; original taskbar restored; no persistent session remains.\n");return 0;
}catch(std::exception const& e){fprintf(stderr,"ManualTrial: %s\n",e.what());return 1;}
