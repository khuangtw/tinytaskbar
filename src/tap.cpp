// SPDX-License-Identifier: GPL-3.0-only
// Background-only adaptation of TranslucentTB 2026.2, commit
// d4636e439865df0a1a1419db408e055740ce5c74. See ../THIRD_PARTY.md.
// v4 candidate: one persistent diagnostics session per Explorer lifetime.
// Normal client quit restores brushes; it does not Unadvise or reinitialize.
#include "protocol.hpp"
#include "broker_lifecycle.hpp"
#include "session_safety.hpp"
#include "primary_pair.hpp"
#include "process_checks.hpp"
#include "controller_checks.hpp"
#include <ocidl.h>
#include <xamlOM.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/base.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <thread>
#include <unordered_set>
#include <cstring>

namespace ux=winrt::Windows::UI::Xaml;
using Queue=winrt::Windows::System::DispatcherQueue;
struct Session;
struct Background : std::enable_shared_from_this<Background> {
    InstanceHandle handle=0;
    DWORD uiThread=0;
    ux::Shapes::Rectangle shape{nullptr};
    ux::Media::Brush original{nullptr},clear{nullptr};
    Queue queue{nullptr};
    std::mutex queueMutex;
    int64_t token=0;
    bool saved=false,registered=false,restoring=false,published=false,border=false,removed=false;
    unsigned writingDepth=0;
    std::atomic<bool> clean{false};
    std::weak_ptr<Session> session;
    bool apply(uint64_t generation,ClientIdentity owner);
    bool restoreOriginal();
    bool dispose();
};
struct Session {
    OwnedHandle mapping,ready,command,changed,wake,owner;
    State* state=nullptr;
    std::mutex ownerMutex;
    BrokerLifecycle<Background> life;
    CallbackDrain callbacks;
    PrimaryPair primary;
    winrt::com_ptr<IVisualTreeService3> tree;
    winrt::com_ptr<IVisualTreeServiceCallback2> watcher;
    LONG observedSequence=0,pendingSequence=0,pendingKind=CommandNone;
    // Deliberate Explorer-lifetime heap anchor. XAML refs are never destroyed
    // on DLL detach or a controller worker; element removal clears them on UI.
    std::shared_ptr<Session>* retention=nullptr;
    ~Session(){if(state)UnmapViewOfFile(state);}
    void signal(){SetEvent(wake.value);}
    void error(HRESULT hr){if(FAILED(hr)){recordError(state,hr);life.fail();signal();}}
    bool ownerAlive(ClientIdentity client){
        std::lock_guard<std::mutex> lock(ownerMutex);
        return life.snapshot().owner==client && owner.value && WaitForSingleObject(owner.value,0)==WAIT_TIMEOUT;
    }
};
static std::atomic<bool> workerStarted{false};
static std::mutex sessionMutex;
static std::weak_ptr<Session> current;
static HMODULE module=nullptr;
struct WakeAfter {
    std::shared_ptr<Session> session;bool force=false;
    ~WakeAfter(){auto state=session->life.snapshot();if(force || state.mode==BrokerMode::Starting || state.mode==BrokerMode::Restoring || state.poisoned)session->signal();}
};

bool Background::apply(uint64_t generation,ClientIdentity client){
    auto s=session.lock();
    if(!s || !s->life.mayApply(generation,client) || !s->ownerAlive(client) || !published || clean || removed || !saved || !shape)return false;
    if(GetCurrentThreadId()!=uiThread)winrt::throw_hresult(RPC_E_WRONG_THREAD);
    auto target=shape;auto brush=clear;
    {WriteDepth writing(writingDepth);target.Fill(brush);}
    if(removed){dispose();return false;}
    if(clean || !s->life.mayApply(generation,client))return false;
    s->primary.applied(*s->state,handle);SetEvent(s->changed.value);return true;
}
bool Background::restoreOriginal(){
    if(clean)return true;
    if(GetCurrentThreadId()!=uiThread)winrt::throw_hresult(RPC_E_WRONG_THREAD);
    {
        RestoreGuard restore(restoring);if(!restore)return false;
        auto target=shape;auto brush=original;WriteDepth writing(writingDepth);
        if(target && saved)target.Fill(brush);
    }
    if(removed)return dispose();
    return true;
}
bool Background::dispose(){
    if(clean)return true;
    if(GetCurrentThreadId()!=uiThread)winrt::throw_hresult(RPC_E_WRONG_THREAD);
    removed=true;
    // A nested Remove waits for the active Fill receiver/restore guard to return.
    if(writingDepth || restoring)return false;
    {
        RestoreGuard restore(restoring);if(!restore)return false;
        auto target=shape;auto brush=original;WriteDepth writing(writingDepth);
        if(registered){target.UnregisterPropertyChangedCallback(ux::Shapes::Shape::FillProperty(),token);registered=false;}
        if(target && saved)target.Fill(brush);
        shape=nullptr;original=nullptr;clear=nullptr;
        {std::lock_guard<std::mutex> lock(queueMutex);queue=nullptr;clean=true;}
    }
    if(auto s=session.lock()){
        s->primary.remove(*s->state,handle);
        if(s->life.removed(shared_from_this())){
            InterlockedDecrement(&s->state->backgrounds);InterlockedDecrement(border?&s->state->strokes:&s->state->fills);
        }
        SetEvent(s->changed.value);
    }
    return true;
}

static void restoreCurrent(std::shared_ptr<Session> const& s,std::shared_ptr<Background> const& bg) noexcept {
    try{
        if(bg->published?bg->restoreOriginal():bg->dispose()){
            s->life.restored(bg);InterlockedIncrement(&s->state->restored);
            InterlockedIncrement(bg->border?&s->state->restoredStrokes:&s->state->restoredFills);
        }else s->error(HRESULT_FROM_WIN32(ERROR_BUSY));
    }catch(...){s->error(winrt::to_hresult());}
}
struct BuildGuard {
    std::shared_ptr<Session> s;std::shared_ptr<Background> bg;bool complete=false;
    ~BuildGuard(){if(!complete){s->life.abortBuild(bg);try{bg->dispose();}catch(...){s->error(winrt::to_hresult());}}}
};
static void dispatch(std::shared_ptr<Session> const& s,BrokerLifecycle<Background>::Batch const& batch,bool restore){
    auto pending=restore?&s->state->restorePending:&s->state->applyPending;
    InterlockedExchange(pending,LONG(batch.entries.size()));
    auto client=s->life.snapshot().owner;
    for(auto const& bg:batch.entries){
        bool queued=false;
        try{
            Queue queue{nullptr};{std::lock_guard<std::mutex> lock(bg->queueMutex);queue=bg->queue;}
            if(queue)queued=queue.TryEnqueue([s,bg,restore,pending,epoch=batch.generation,client]{
                WakeAfter wake{s,true};
                trackedCallback(s->callbacks,[&](bool admitted){
                    if(admitted){
                        if(restore)restoreCurrent(s,bg);
                        else if(bg->apply(epoch,client)){
                            InterlockedIncrement(&s->state->applied);
                            InterlockedIncrement(bg->border?&s->state->appliedStrokes:&s->state->appliedFills);
                        }
                    }
                },[&]{s->error(winrt::to_hresult());});
                InterlockedDecrement(pending);s->life.workFinished();
            });
        }catch(...){s->error(winrt::to_hresult());}
        if(!queued){
            // A removed element has already restored and released its UI refs.
            if(!bg->clean)s->error(HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID));
            else if(restore)s->life.restored(bg);
            InterlockedDecrement(pending);s->life.workFinished();s->signal();
        }
    }
    s->signal();
}

struct Watcher : winrt::implements<Watcher,IVisualTreeServiceCallback2,winrt::non_agile> {
    winrt::com_ptr<IXamlDiagnostics> diagnostics;
    std::weak_ptr<Session> session;
    std::mutex sourcesMutex;
    std::unordered_set<InstanceHandle> sources;
    Watcher(winrt::com_ptr<IUnknown> const& site,std::shared_ptr<Session> const& s):diagnostics(site.as<IXamlDiagnostics>()),session(s){}
    template<class T>T from(InstanceHandle handle){
        winrt::Windows::Foundation::IInspectable object;
        winrt::check_hresult(diagnostics->GetIInspectableFromHandle(handle,reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
        return object.as<T>();
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle,VisualElementState,LPCWSTR) noexcept override try{
        auto lifetime=get_strong();if(auto s=session.lock()){
            WakeAfter wake{s};trackedCallback(s->callbacks,[](bool){},[&]{s->error(winrt::to_hresult());});
        }return S_OK;
    }catch(...){return S_OK;}
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation,VisualElement element,VisualMutationType mutation) noexcept override try{
        auto lifetime=get_strong();
        struct Strings {VisualElement& e;~Strings(){SysFreeString(e.SrcInfo.FileName);SysFreeString(e.SrcInfo.Hash);SysFreeString(e.Name);SysFreeString(e.Type);}} strings{element};
        auto s=session.lock();if(!s)return S_OK;WakeAfter wake{s};
        trackedCallback(s->callbacks,[&](bool admitted){
            if(!admitted)return;
            if(mutation==Remove){
                {std::lock_guard<std::mutex> lock(sourcesMutex);sources.erase(element.Handle);}
                if(auto old=s->life.lookup(element.Handle)){
                    old->removed=true;s->primary.remove(*s->state,element.Handle);
                    // Nested Remove is completed by its enclosing write before
                    // the tracked callback/queue ticket can acknowledge restore.
                    if(!old->writingDepth && !old->restoring)old->dispose();
                }
                return;
            }
            if(s->life.snapshot().poisoned)return;
            std::wstring_view type(element.Type?element.Type:L"",SysStringLen(element.Type));
            if(type==L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource"){
                std::lock_guard<std::mutex> lock(sourcesMutex);if(sources.size()<64)sources.insert(element.Handle);return;
            }
            if(type!=L"Windows.UI.Xaml.Shapes.Rectangle" || !element.Name)return;
            bool border=wcscmp(element.Name,L"BackgroundStroke")==0;
            if(!border && wcscmp(element.Name,L"BackgroundFill"))return;
            auto frame=from<ux::FrameworkElement>(relation.Parent);
            for(unsigned i=0;i<32 && frame && frame.Name()!=L"TaskbarFrame";++i)frame=ux::Media::VisualTreeHelper::GetParent(frame).try_as<ux::FrameworkElement>();
            if(!frame || frame.Name()!=L"TaskbarFrame" || winrt::get_class_name(frame)!=L"Taskbar.TaskbarFrame")return;
            auto root=ux::Media::VisualTreeHelper::GetParent(frame).try_as<ux::UIElement>();HWND island=nullptr;
            std::vector<InstanceHandle> candidates;{std::lock_guard<std::mutex> lock(sourcesMutex);candidates.assign(sources.begin(),sources.end());}
            for(auto handle:candidates)try{
                auto source=from<ux::Hosting::DesktopWindowXamlSource>(handle);if(source.Content()!=root)continue;
                winrt::check_hresult(source.as<IDesktopWindowXamlSourceNative>()->get_WindowHandle(&island));break;
            }catch(winrt::hresult_wrong_thread const&){}
            if(!island)return;
            HWND bar=GetAncestor(island,GA_PARENT);wchar_t cls[128]={};GetClassNameW(bar,cls,128);DWORD pid=0;GetWindowThreadProcessId(bar,&pid);
            if(pid!=s->state->targetPid || (wcscmp(cls,L"Shell_TrayWnd") && wcscmp(cls,L"Shell_SecondaryTrayWnd")))return;
            if(s->life.lookup(element.Handle))return;
            auto bg=std::make_shared<Background>();bg->handle=element.Handle;bg->session=s;bg->uiThread=GetCurrentThreadId();bg->border=border;
            if(!s->life.begin(bg))return;BuildGuard guard{s,bg};UINT_PTR frameIdentity=0;
            {
            // Register may be reentrant. Defer a nested Remove until its token
            // and all newly acquired members are owned and the call has returned.
            WriteDepth construction(bg->writingDepth);
            bg->shape=from<ux::Shapes::Rectangle>(element.Handle);bg->original=bg->shape.Fill();bg->saved=true;
            if(border && wcscmp(cls,L"Shell_TrayWnd")==0){
                auto point=bg->shape.TransformToVisual(root).TransformPoint({0,0});auto xamlRoot=bg->shape.XamlRoot();winrt::check_hresult(interfaceResult(bool(xamlRoot)));
                s->state->strokeX=point.X;s->state->strokeY=point.Y;s->state->strokeWidth=bg->shape.ActualWidth();s->state->strokeHeight=bg->shape.ActualHeight();s->state->rasterScale=xamlRoot.RasterizationScale();
            }
            bg->queue=Queue::GetForCurrentThread();winrt::check_hresult(interfaceResult(bool(bg->queue)));
            winrt::check_hresult(interfaceResult(bool(bg->queue.try_as<IAgileObject>())));
            ux::Media::SolidColorBrush clear;clear.Color(winrt::Windows::UI::Color{0,0,0,0});bg->clear=clear;
            std::weak_ptr<Background> weak=bg;
            bg->token=bg->shape.RegisterPropertyChangedCallback(ux::Shapes::Shape::FillProperty(),[weak](auto const&,auto const&){
                if(auto b=weak.lock())if(auto owner=b->session.lock()){
                    WakeAfter wake{owner};trackedCallback(owner->callbacks,[&](bool allowed){
                        if(!allowed || b->writingDepth || b->clean || b->removed)return;
                        if(GetCurrentThreadId()!=b->uiThread)winrt::throw_hresult(RPC_E_WRONG_THREAD);
                        // While inactive keep the newest Windows/theme brush.
                        b->original=b->shape.Fill();b->saved=true;auto snapshot=owner->life.snapshot();
                        if(snapshot.active)b->apply(snapshot.generation,snapshot.owner);
                    },[&]{owner->error(winrt::to_hresult());});
                }
            });bg->registered=true;
            if(!bg->removed)bg->original=bg->shape.Fill();
            frameIdentity=reinterpret_cast<UINT_PTR>(frame.as<IUnknown>().get());
            }
            if(bg->removed || !s->life.publish(bg)){s->life.abortBuild(bg);bg->dispose();guard.complete=true;return;}
            bg->published=true;InterlockedIncrement(&s->state->backgrounds);InterlockedIncrement(border?&s->state->strokes:&s->state->fills);
            if(wcscmp(cls,L"Shell_TrayWnd")==0)s->primary.rebind(*s->state,reinterpret_cast<UINT_PTR>(bar));
            s->primary.add(*s->state,bg->handle,reinterpret_cast<UINT_PTR>(bar),frameIdentity,border);
            guard.complete=true;SetEvent(s->changed.value);auto snapshot=s->life.snapshot();if(snapshot.active)bg->apply(snapshot.generation,snapshot.owner);
        },[&]{s->error(winrt::to_hresult());});return S_OK;
    }catch(...){return winrt::to_hresult();}
};

struct SubscriptionWork {std::shared_ptr<Session> session;winrt::com_ptr<IVisualTreeService3> tree;winrt::com_ptr<IVisualTreeServiceCallback2> watcher;HMODULE reference=nullptr;};
static DWORD WINAPI subscriptionWorker(void* argument){
    std::unique_ptr<SubscriptionWork> work(static_cast<SubscriptionWork*>(argument));HMODULE reference=work->reference;auto s=work->session;
    try{
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        HRESULT hr=work->tree->AdviseVisualTreeChange(work->watcher.get());
        if(FAILED(hr))s->error(hr);s->life.initialized(SUCCEEDED(hr));
        // Session retains tree/watcher forever. No client-owned Unadvise, no
        // last watcher release here; only verified-agile external refs copied.
        work->tree=nullptr;work->watcher=nullptr;winrt::uninit_apartment();
    }catch(...){s->error(winrt::to_hresult());s->life.initialized(false);}
    s->signal();work.reset();s.reset();FreeLibraryAndExitThread(reference,0);
}
struct Site : winrt::implements<Site,IObjectWithSite,winrt::non_agile> {
    winrt::com_ptr<IUnknown> site;
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid,void** out) noexcept override{return site.as(iid,out);}
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* value) noexcept override try{
        if(!value){site=nullptr;return S_OK;}
        std::shared_ptr<Session> s;{std::lock_guard<std::mutex> lock(sessionMutex);s=current.lock();}
        if(!s || !s->life.claimSubscription())return E_ABORT;
        try{
            winrt::com_ptr<IUnknown> candidate;candidate.copy_from(value);
            winrt::check_hresult(interfaceResult(bool(candidate.try_as<IAgileObject>())));
            auto tree=candidate.as<IVisualTreeService3>();auto diagnostics=candidate.as<IXamlDiagnostics>();
            winrt::check_hresult(interfaceResult(bool(tree.try_as<IAgileObject>())));winrt::check_hresult(interfaceResult(bool(diagnostics.try_as<IAgileObject>())));
            site=candidate;auto watcher=winrt::make_self<Watcher>(site,s);
            auto work=std::make_unique<SubscriptionWork>();work->session=s;work->tree=tree;work->watcher=watcher.as<IVisualTreeServiceCallback2>();
            // Only this Site writes these members, before publishing ready.
            s->tree=tree;s->watcher=work->watcher;
            winrt::check_bool(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&subscriptionWorker),&work->reference));
            HANDLE thread=CreateThread(nullptr,0,subscriptionWorker,work.get(),0,nullptr);
            if(!thread){DWORD error=GetLastError();FreeLibrary(work->reference);winrt::throw_hresult(HRESULT_FROM_WIN32(error));}
            work.release();CloseHandle(thread);return S_OK;
        }catch(...){HRESULT hr=winrt::to_hresult();s->error(hr);s->life.initialized(false);return hr;}
    }catch(...){return winrt::to_hresult();}
};
struct Factory : winrt::implements<Factory,IClassFactory> {
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override{return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) noexcept override try{
        if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;return winrt::make_self<Site>().as(iid,out);
    }catch(...){return winrt::to_hresult();}
};
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void** out) try{
    if(!out)return E_POINTER;*out=nullptr;if(clsid!=TapClass)return CLASS_E_CLASSNOTAVAILABLE;return winrt::make_self<Factory>().as(iid,out);
}catch(...){return winrt::to_hresult();}
extern "C" HRESULT __stdcall DllCanUnloadNow(){return S_FALSE;}

static void publish(std::shared_ptr<Session> const& s){
    auto snapshot=s->life.snapshot();
    LONG status=StatusStarting;
    switch(snapshot.mode){case BrokerMode::Idle:status=StatusIdle;break;case BrokerMode::Attached:status=StatusAttached;break;case BrokerMode::Restoring:status=StatusRestoring;break;case BrokerMode::Poisoned:case BrokerMode::Gone:status=StatusPoisoned;break;default:break;}
    s->state->ownerPid=snapshot.owner.pid;s->state->ownerCreation=snapshot.owner.birth;s->state->generation=snapshot.generation;
    InterlockedExchange(&s->state->status,status);if(status!=StatusStarting)SetEvent(s->ready.value);
    SetEvent(s->changed.value);
}
static void respond(std::shared_ptr<Session> const& s,LONG sequence,HRESULT hr){
    publish(s);InterlockedExchange(&s->state->responseError,LONG(hr));InterlockedExchange(&s->state->responseSequence,sequence);SetEvent(s->changed.value);
}
static void beginRestore(std::shared_ptr<Session> const& s,bool death=false,bool failure=false){
    auto batch=s->life.restore(s->life.snapshot().owner,death,failure);if(!batch.allowed)return;
    InterlockedExchange(&s->state->cleanupComplete,0);InterlockedExchange(&s->state->cleanupRetained,1);
    publish(s);dispatch(s,batch,true);
}
static void progress(std::shared_ptr<Session> const& s){
    auto snapshot=s->life.snapshot();
    if(snapshot.poisoned && snapshot.mode!=BrokerMode::Restoring && snapshot.mode!=BrokerMode::Poisoned)beginRestore(s,false,true);
    if(s->life.finishRestore(s->callbacks)){
        s->primary.clearApplied(*s->state);InterlockedExchange(&s->state->cleanupRetained,0);InterlockedExchange(&s->state->cleanupComplete,1);
        {std::lock_guard<std::mutex> lock(s->ownerMutex);s->owner.reset();}
        publish(s);
        if(s->pendingKind==CommandStop){
            HRESULT hr=readLong(s->state->error);
            if(s->life.snapshot().poisoned && SUCCEEDED(hr))hr=E_FAIL;
            respond(s,s->pendingSequence,hr);s->pendingKind=CommandNone;
        }
    }
    snapshot=s->life.snapshot();
    if(s->pendingKind==CommandApply && !snapshot.pending && s->callbacks.quiescent()){
        HRESULT hr=snapshot.mode==BrokerMode::Attached && !snapshot.poisoned?S_OK:HRESULT_FROM_WIN32(ERROR_CANCELLED);
        respond(s,s->pendingSequence,hr);s->pendingKind=CommandNone;
    }
    publish(s);
}
static void request(std::shared_ptr<Session> const& s){
    LONG sequence=readLong(s->state->requestSequence);if(!sequence || sequence==s->observedSequence)return;
    s->observedSequence=sequence;ClientIdentity client{s->state->requestPid,s->state->requestCreation};LONG command=readLong(s->state->command);
    if(!compatibleRequest(*s->state)){
        respond(s,sequence,HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH));return;
    }
    if(command==CommandAttach){
        OwnedHandle process;process.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,client.pid);
        bool valid=false;try{if(process.value){sameUserSession(process.value);valid=processBirth(process.value)==client.birth && WaitForSingleObject(process.value,0)==WAIT_TIMEOUT;}}catch(...){valid=false;}
        if(!s->life.attach(client,true,valid)){respond(s,sequence,HRESULT_FROM_WIN32(ERROR_BUSY));return;}
        {std::lock_guard<std::mutex> lock(s->ownerMutex);s->owner.reset(process.value);process.value=nullptr;}
        for(auto p:{&s->state->applied,&s->state->restored,&s->state->appliedFills,&s->state->appliedStrokes,&s->state->restoredFills,&s->state->restoredStrokes,&s->state->cleanupComplete,&s->state->cleanupRetained})InterlockedExchange(p,0);
        s->primary.clearApplied(*s->state);respond(s,sequence,S_OK);return;
    }
    if(!(s->life.snapshot().owner==client) || !s->ownerAlive(client)){respond(s,sequence,E_ACCESSDENIED);return;}
    if(command==CommandApply){
        auto batch=s->life.apply(client);if(!batch.allowed){respond(s,sequence,E_ABORT);return;}
        s->pendingKind=command;s->pendingSequence=sequence;if(batch.entries.empty())s->error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));dispatch(s,batch,false);
    }else if(command==CommandStop){
        s->pendingKind=command;s->pendingSequence=sequence;beginRestore(s);
        // A repeated stop can observe a restore already underway.
        if(s->life.snapshot().mode==BrokerMode::Poisoned)respond(s,sequence,E_FAIL);
    }else respond(s,sequence,E_INVALIDARG);
}
static DWORD workerRun() noexcept try{
    auto s=std::make_shared<Session>();DWORD pid=GetCurrentProcessId();ULONGLONG birth=processBirth(GetCurrentProcess());
    s->mapping.value=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,objectName(pid,birth,L"state").c_str());if(!s->mapping.value)return ERROR_INVALID_HANDLE;
    s->state=static_cast<State*>(MapViewOfFile(s->mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(State)));
    if(!s->state || s->state->magic!=ProtocolMagic || s->state->version!=ProtocolVersion || s->state->bytes!=sizeof(State) || s->state->build!=BrokerBuild || std::memcmp(s->state->revision,BrokerRevision,32) || s->state->targetPid!=pid || s->state->targetCreation!=birth)return ERROR_INVALID_DATA;
    wchar_t image[32768]={},windows[MAX_PATH]={};GetModuleFileNameW(nullptr,image,32768);GetWindowsDirectoryW(windows,MAX_PATH);
    DWORD actual=0;GetWindowThreadProcessId(reinterpret_cast<HWND>(s->state->taskbar),&actual);
    if(_wcsicmp(image,(std::wstring(windows)+L"\\explorer.exe").c_str()) || actual!=pid)return ERROR_ACCESS_DENIED;
    OwnedHandle bootstrap;bootstrap.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,s->state->bootstrapPid);
    if(!bootstrap.value || processBirth(bootstrap.value)!=s->state->bootstrapCreation || WaitForSingleObject(bootstrap.value,0)!=WAIT_TIMEOUT)return ERROR_ACCESS_DENIED;
    sameUserSession(bootstrap.value);
    auto event=[&](wchar_t const* suffix){return OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,objectName(pid,birth,suffix).c_str());};
    s->ready.value=event(L"ready");s->command.value=event(L"command");s->changed.value=event(L"changed");s->wake.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!s->ready.value || !s->command.value || !s->changed.value || !s->wake.value)return ERROR_INVALID_HANDLE;
    HMODULE pin=nullptr;require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&workerRun),&pin)!=FALSE,"Cannot retain native broker.");
    s->retention=new std::shared_ptr<Session>(s);{std::lock_guard<std::mutex> lock(sessionMutex);current=s;}
    if(!s->life.beginInitialization())return ERROR_ALREADY_INITIALIZED;
    InterlockedIncrement(&s->state->initializations);
    HMODULE xaml=LoadLibraryExW(L"Windows.UI.Xaml.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto init=xaml?reinterpret_cast<decltype(&InitializeXamlDiagnosticsEx)>(GetProcAddress(xaml,"InitializeXamlDiagnosticsEx")):nullptr;
    HRESULT hr=init?E_FAIL:HRESULT_FROM_WIN32(GetLastError());wchar_t path[32768]={};GetModuleFileNameW(module,path,32768);
    if(init)std::thread([&]{hr=init(L"VisualDiagConnection1",pid,nullptr,path,TapClass,nullptr);}).join();
    if(xaml)FreeLibrary(xaml);if(FAILED(hr)){s->error(hr);s->life.initialized(false);}
    publish(s);
    // Resident native worker sleeps indefinitely on command, callback completion
    // and the attached client's real process handle. No timer or busy polling.
    for(;;){
        progress(s);HANDLE handles[]={s->command.value,s->wake.value,s->owner.value};
        // A dead owner is permanently signalled. Exclude it once restoring,
        // otherwise the worker would spin while its UI restore is pending.
        DWORD count=s->owner.value && s->life.watchesOwner()?3:2;
        DWORD result=WaitForMultipleObjects(count,handles,FALSE,INFINITE);
        if(result==WAIT_OBJECT_0)request(s);
        else if(result==WAIT_OBJECT_0+2)beginRestore(s,true);
        else if(result!=WAIT_OBJECT_0+1){s->error(HRESULT_FROM_WIN32(GetLastError()));beginRestore(s,false,true);return ERROR_INVALID_HANDLE;}
    }
}catch(...){
    HRESULT hr=winrt::to_hresult();std::shared_ptr<Session> s;
    {std::lock_guard<std::mutex> lock(sessionMutex);s=current.lock();}
    if(s)try{s->error(hr);beginRestore(s,false,true);publish(s);}catch(...){}
    return DWORD(hr);
}
static DWORD WINAPI worker(void* reference){DWORD result=workerRun();FreeLibraryAndExitThread(static_cast<HMODULE>(reference),result);}
extern "C" LRESULT CALLBACK TinyTaskbarHook(int code,WPARAM wp,LPARAM lp) noexcept{
    if(code>=0 && lp && reinterpret_cast<CWPSTRUCT*>(lp)->message==RegisterWindowMessageW(BeginMessage)){
        bool unused=false;if(workerStarted.compare_exchange_strong(unused,true)){
            HMODULE reference=nullptr;
            if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&TinyTaskbarHook),&reference)){
                HANDLE thread=CreateThread(nullptr,0,worker,reference,0,nullptr);if(thread)CloseHandle(thread);else FreeLibrary(reference);
            }
        }
    }return CallNextHookEx(nullptr,code,wp,lp);
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH)module=instance;return TRUE;}
