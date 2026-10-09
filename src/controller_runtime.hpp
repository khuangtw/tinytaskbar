// SPDX-License-Identifier: GPL-3.0-only
// Included by manual.cpp after its logging/validation helpers.
static bool waitTaskbar(DWORD milliseconds){
    ULONGLONG end=GetTickCount64()+milliseconds;
    for(;;){
        pumpOwnMessages();if(quitRequested)return false;
        if(HWND bar=FindWindowW(L"Shell_TrayWnd",nullptr)){
            DWORD pid=0;GetWindowThreadProcessId(bar,&pid);OwnedHandle owner;
            if(pid)owner.value=OpenProcess(SYNCHRONIZE,FALSE,pid);
            if(owner.value && WaitForSingleObject(owner.value,0)==WAIT_TIMEOUT)return true;
        }
        ULONGLONG now=GetTickCount64();if(now>=end)return false;
        DWORD result=MsgWaitForMultipleObjectsEx(0,nullptr,DWORD(end-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if(result==WAIT_FAILED || result==WAIT_TIMEOUT)return false;
        // Window messages, especially TaskbarCreated, wake this bounded wait.
        // No timer, sleep loop or desktop polling when there are no messages.
    }
}
struct Ipc {
    OwnedHandle mapping,ready,command,changed;
    State* state=nullptr;HANDLE explorer=nullptr;
    DWORD target=0;ULONGLONG birth=0;
    bool attached=false,stopped=false,stopAttempted=false;
    ~Ipc(){
        if(attached && !stopped && !stopAttempted)try{stop();}catch(std::exception const& e){log(std::string("Restore not acknowledged: ")+e.what());}
        if(state)UnmapViewOfFile(state);
    }
    template<class Predicate>bool waitState(Predicate predicate,DWORD milliseconds,bool cancel=true){
        ULONGLONG end=GetTickCount64()+milliseconds;
        for(;;){
            if(predicate())return true;
            pumpOwnMessages();if(cancel && quitRequested)return false;
            if(WaitForSingleObject(explorer,0)!=WAIT_TIMEOUT)return false;
            ULONGLONG now=GetTickCount64();if(now>=end)return false;
            HANDLE handles[]={changed.value,explorer};
            DWORD result=MsgWaitForMultipleObjectsEx(2,handles,DWORD(end-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            if(result==WAIT_FAILED || result==WAIT_TIMEOUT || result==WAIT_OBJECT_0+1)return false;
        }
    }
    void open(DWORD pid,HWND bar,HANDLE process,bool reconnect){
        target=pid;birth=processBirth(process);explorer=process;
        auto name=objectName(pid,birth,L"state");
        if(reconnect)mapping.value=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
        else{
            mapping.value=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(State),name.c_str());
            require(mapping.value && GetLastError()!=ERROR_ALREADY_EXISTS,"Existing broker IPC will not be overwritten or initialized again.");
        }
        require(mapping.value!=nullptr,"Loaded broker has no reachable session; refusing reinitialization.");
        state=static_cast<State*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(State)));require(state!=nullptr,"Cannot map broker IPC.");
        if(!reconnect){
            ZeroMemory(state,sizeof(State));state->magic=ProtocolMagic;state->version=ProtocolVersion;state->bytes=sizeof(State);
            state->targetPid=pid;state->targetCreation=birth;state->build=BrokerBuild;std::memcpy(state->revision,BrokerRevision,32);
            state->taskbar=UINT_PTR(bar);state->bootstrapPid=GetCurrentProcessId();state->bootstrapCreation=processBirth(GetCurrentProcess());
        }
        require(compatibleState(*state,pid,birth),"Broker version, source revision or Explorer identity differs; no retry of diagnostics initialization.");
        auto event=[&](OwnedHandle& handle,wchar_t const* suffix,BOOL manual){
            auto eventName=objectName(pid,birth,suffix);
            if(reconnect)handle.value=OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,eventName.c_str());
            else{
                handle.value=CreateEventW(nullptr,manual,FALSE,eventName.c_str());
                require(handle.value && GetLastError()!=ERROR_ALREADY_EXISTS,"IPC event collision; preserving the existing object.");
            }
            require(handle.value!=nullptr,"Broker event is unavailable; refusing initialization retry.");
        };
        event(ready,L"ready",TRUE);event(command,L"command",FALSE);event(changed,L"changed",FALSE);
    }
    LONG send(LONG kind){
        state->requestPid=GetCurrentProcessId();state->requestCreation=processBirth(GetCurrentProcess());state->requestVersion=ProtocolVersion;
        std::memcpy(state->requestRevision,BrokerRevision,32);InterlockedExchange(&state->command,kind);
        LONG sequence=InterlockedIncrement(&state->requestSequence);if(!sequence)sequence=InterlockedIncrement(&state->requestSequence);
        require(SetEvent(command.value)!=FALSE,"Cannot signal broker command.");return sequence;
    }
    void acknowledged(LONG sequence,DWORD timeout,bool cancel=true){
        require(waitState([&]{return readLong(state->responseSequence)==sequence;},timeout,cancel),"Broker command cancelled, Explorer exited or acknowledgement timed out.");
        require(readLong(state->responseError)==0,"Broker rejected command; inspect its recorded status.");
    }
    void attach(){
        require(waitState([&]{return readLong(state->status)!=StatusStarting && readLong(state->status)!=StatusRestoring;},20000),"Broker readiness/previous restore did not complete.");
        require(readLong(state->status)==StatusIdle && !readLong(state->error),"Broker is occupied or poisoned; initialization will not be retried.");
        LONG sequence=send(CommandAttach);acknowledged(sequence,6000);attached=true;
        require(state->ownerPid==GetCurrentProcessId() && state->ownerCreation==processBirth(GetCurrentProcess()),"Broker owner acknowledgement does not match this controller.");
        require(waitState([&]{return primaryReady(*state) || readLong(state->error)!=0;},20000) && primaryReady(*state),"Live primary fill/stroke pair is unavailable; restoring.");
        sequence=send(CommandApply);acknowledged(sequence,6000);
        require(primaryReady(*state,true),"Primary fill/stroke application was not acknowledged; restoring.");
        log("TRANSPARENT_READY: broker attach generation="+std::to_string(state->generation)+" diagnosticsInitializations="+std::to_string(readLong(state->initializations)));
    }
    void stop(){
        if(!attached || stopped || stopAttempted)return;
        stopAttempted=true;
        if(WaitForSingleObject(explorer,0)==WAIT_OBJECT_0){stopped=true;log("Explorer exited naturally; old broker ended with it.");return;}
        LONG sequence=send(CommandStop);acknowledged(sequence,6000,false);
        require(readLong(state->status)==StatusIdle && !readLong(state->error) && readLong(state->cleanupComplete)==1 && readLong(state->cleanupRetained)==0 && readLong(state->applyPending)==0 && readLong(state->restorePending)==0,"Restore/drain has not completed; broker remains fail-closed.");
        stopped=true;log("Original brushes restored; client exits. Broker remains inactive for safe reconnect.");
    }
};
struct Library {
    HMODULE module=nullptr;HHOOK hook=nullptr;OwnedHandle thread;
    ~Library(){if(hook)UnhookWindowsHookEx(hook);if(module)FreeLibrary(module);}
};
struct Runtime {
    OwnedHandle explorer;OwnedMutex instance;Ipc ipc;Library library;std::unique_ptr<TrustedDllFile> trusted;
    DWORD target=0;HWND bar=nullptr;
    void start(std::wstring const& executable){
        require(waitTaskbar(30000),"Taskbar did not become available within 30 seconds, or startup was cancelled.");
        DWORD tid=validate(bar,explorer);target=GetProcessId(explorer.value);ULONGLONG birth=processBirth(explorer.value);
        claimMutex(instance,objectName(target,birth,L"client"));
        require(!FindWindowW(L"TTB_WorkerWindow",nullptr),"TranslucentTB is running; quit it normally before testing this candidate.");
        auto selection=selectDll(explorer.value,executable);trusted=std::make_unique<TrustedDllFile>(selection.path);
        ipc.open(target,bar,explorer.value,selection.reconnect);
        if(!selection.reconnect){
            library.module=LoadLibraryExW(trusted->path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
            require(library.module!=nullptr,"Cannot load this paired broker DLL locally.");
            FARPROC raw=GetProcAddress(library.module,"TinyTaskbarHook");HOOKPROC hook=nullptr;static_assert(sizeof(hook)==sizeof(raw),"Pointer size");std::memcpy(&hook,&raw,sizeof(hook));require(hook!=nullptr,"Broker hook export is missing.");
            DWORD pid=0;require(GetWindowThreadProcessId(bar,&pid)==tid && pid==target,"Explorer/taskbar changed before initialization.");
            library.thread.value=OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,tid);
            require(library.thread.value && GetProcessIdOfThread(library.thread.value)==target && WaitForSingleObject(library.thread.value,0)==WAIT_TIMEOUT,"Taskbar UI thread changed before initialization.");
            library.hook=SetWindowsHookExW(WH_CALLWNDPROC,hook,library.module,tid);require(library.hook!=nullptr,"Temporary initialization hook failed.");
            DWORD_PTR result=0;require(SendMessageTimeoutW(bar,RegisterWindowMessageW(BeginMessage),0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&result)!=0,"Initialization message timed out.");
            bool ready=waitStartup(ipc.ready.value,20000);
            require(UnhookWindowsHookEx(library.hook)!=FALSE,"Cannot remove temporary initialization hook.");library.hook=nullptr;
            require(ready,"Diagnostics initialization did not acknowledge; no retry in this Explorer.");
        }
        // Reconnect branch never loads a local DLL, installs a hook or calls Init.
        ipc.attach();
    }
};
