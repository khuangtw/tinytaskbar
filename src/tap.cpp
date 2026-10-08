// Minimal background-only extraction of TranslucentTB 2026.2, GPL-3.0.
// Adapted 2026-10-07 from tapsite.cpp, visualtreewatcher.cpp and
// taskbarappearanceservice.cpp, commit d4636e439865df0a1a1419db408e055740ce5c74.
// Charles Milette, Ethan Smith and TranslucentTB contributors; see ../THIRD_PARTY.md.
// v3 also clears the separate BackgroundStroke fill; both brushes are restored.
// No icon/text/control/window opacity, size or behavior is changed.
// Modified 2026-10-08: one initialization per Explorer, callback drain, explicit
// COM agility/release checks and nested brush-write ownership. Native fix unverified.
#include "protocol.hpp"
#include "lifecycle.hpp"
#include "primary_pair.hpp"
#include "session_safety.hpp"
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

namespace ux=winrt::Windows::UI::Xaml;
using Queue=winrt::Windows::System::DispatcherQueue;
struct Session;
struct Background {
    InstanceHandle handle=0;
    HWND island=nullptr;
    DWORD uiThread=0;
    ux::Shapes::Rectangle shape{nullptr};
    ux::Media::Brush original{nullptr},clear{nullptr};
    Queue queue{nullptr};
    std::mutex queueMutex;
    int64_t token=0;
    bool saved=false,registered=false,restoring=false,published=false,border=false;
    unsigned writingDepth=0;
    std::atomic<bool> clean{false};
    std::weak_ptr<Session> session;
    bool apply();
    bool restore();
};
struct Session {
    HANDLE mapping=nullptr,ready=nullptr,command=nullptr,appliedEvent=nullptr,done=nullptr;
    HANDLE subscriptionStop=nullptr,owner=nullptr;
    State* state=nullptr;
    std::atomic<bool> active{false};
    Lifecycle<Background> life;
    CallbackDrain callbacks;
    ComReleasePolicy releasePolicy;
    PrimaryPair primary;
    winrt::com_ptr<IVisualTreeService3> tree;
    winrt::com_ptr<IVisualTreeServiceCallback2> watcher;
    // Rooted before any XAML ownership. Failed cleanup stays rooted until natural
    // Explorer exit; its C++ destructor is deliberately never run by DLL detach.
    // Successful cleanup removes this root after all saved UI refs are cleared.
    std::shared_ptr<Session>* retention=nullptr;
    ~Session() {
        if(state)UnmapViewOfFile(state);
        for(HANDLE h:{mapping,ready,command,appliedEvent,done,subscriptionStop,owner})if(h)CloseHandle(h);
    }
    void error(HRESULT hr){recordError(state,hr);}
};
static std::atomic<bool> running{false};
static InitializationGate initialization;
static std::mutex sessionMutex;
static std::weak_ptr<Session> current;
static HMODULE module=nullptr;

static void tryFinish(std::shared_ptr<Session> const& s) {
    bool complete=s->life.acknowledge([&]{
        return InterlockedCompareExchange(&s->state->applyPending,0,0)==0 &&
               InterlockedCompareExchange(&s->state->restorePending,0,0)==0 &&
               s->callbacks.drained();
    },[&]{
        InterlockedExchange(&s->state->cleanupRetained,0);
        InterlockedExchange(&s->state->cleanupComplete,1);
        SetEvent(s->done);
    });
    if(complete){auto anchor=s->retention;s->retention=nullptr;delete anchor;}
}
bool Background::apply() {
    auto s=session.lock();
    if(!s || s->life.stopping || !s->active || !published || clean || !saved || !shape ||
       WaitForSingleObject(s->owner,0)!=WAIT_TIMEOUT)return false;
    if(GetCurrentThreadId()!=uiThread)winrt::throw_hresult(RPC_E_WRONG_THREAD);
    // Keep call receiver/argument alive across a nested Remove or Fill callback.
    auto target=shape;auto brush=clear;WriteDepth writing(writingDepth);
    target.Fill(brush);
    if(clean || s->life.stopping || !s->active)return false;
    s->primary.applied(*s->state,handle);return true;
}
bool Background::restore() {
    if(clean)return true;
    if(GetCurrentThreadId()!=uiThread)winrt::throw_hresult(RPC_E_WRONG_THREAD);
    // A nested Remove during restoring Fill must not publish successful cleanup.
    RestoreGuard restore(restoring);if(!restore)return false;
    // On failure retain every saved ref and token state; never release these on
    // a worker. A retry of an accepted queued callback is idempotent.
    WriteDepth writing(writingDepth);
    auto target=shape;auto brush=original;
        if(registered){shape.UnregisterPropertyChangedCallback(ux::Shapes::Shape::FillProperty(),token);registered=false;}
        if(target && saved)target.Fill(brush);
        shape=nullptr;original=nullptr;clear=nullptr;
        {std::lock_guard<std::mutex> lock(queueMutex);queue=nullptr;clean=true;}
    return true;
}
static void restoreCurrent(std::shared_ptr<Session> const& s,std::shared_ptr<Background> const& bg) noexcept {
    try {
        // Removal invalidates readiness even if original-brush restoration fails.
        s->primary.remove(*s->state,bg->handle);
        if(bg->restore() && s->life.cleaned(bg)){
            InterlockedIncrement(&s->state->restored);
            InterlockedIncrement(bg->border?&s->state->restoredStrokes:&s->state->restoredFills);
        }
    }catch(...){s->error(winrt::to_hresult());}
    // A failed entry remains owned by life and blocks done. Pending entries
    // accepted by DispatcherQueue keep this root and may finish after timeout.
    tryFinish(s);
}
struct BuildGuard {
    std::shared_ptr<Session> s;
    std::shared_ptr<Background> bg;
    bool complete=false;
    ~BuildGuard(){if(!complete){s->life.abortBuild(bg);restoreCurrent(s,bg);}}
};
static void dispatch(std::shared_ptr<Session> const& s,std::vector<std::shared_ptr<Background>> const& entries,bool restore) {
    auto pending=restore?&s->state->restorePending:&s->state->applyPending;
    if(entries.empty()){if(!restore)SetEvent(s->appliedEvent);tryFinish(s);return;}
    for(auto const& bg:entries) {
        bool queued=false;
        try {
            Queue queue{nullptr};
            {std::lock_guard<std::mutex> lock(bg->queueMutex);queue=bg->queue;}
            if(queue)queued=queue.TryEnqueue([s,bg,restore,pending]{
                if(restore)restoreCurrent(s,bg);
                else try {
                    if(!s->life.stopping){
                        if(bg->apply()){
                            InterlockedIncrement(&s->state->applied);
                            InterlockedIncrement(bg->border?&s->state->appliedStrokes:&s->state->appliedFills);
                        }
                        else s->error(HRESULT_FROM_WIN32(ERROR_NOT_READY));
                    }
                }catch(...){s->error(winrt::to_hresult());}
                if(InterlockedDecrement(pending)==0 && !restore)SetEvent(s->appliedEvent);
                tryFinish(s);
            });
        }catch(...){s->error(winrt::to_hresult());}
        if(!queued){
            if(!bg->clean && (restore || !s->life.stopping))s->error(HRESULT_FROM_WIN32(ERROR_INVALID_THREAD_ID));
            if(InterlockedDecrement(pending)==0 && !restore)SetEvent(s->appliedEvent);
            tryFinish(s); // Retained entry prevents a false successful stop.
        }
    }
}
static void requestStop(std::shared_ptr<Session> const& s) {
    auto batch=s->life.beginStop([&](size_t count){
        s->active=false;
        InterlockedExchange(&s->state->stopping,1);
        InterlockedExchange(&s->state->cleanupRetained,1);
        InterlockedExchange(&s->state->restorePending,LONG(count));
    });
    s->callbacks.close();
    SetEvent(s->subscriptionStop);
    if(batch.allowed)dispatch(s,batch.entries,true);
    tryFinish(s);
}
static void stop(std::shared_ptr<Session> const& s) {
    requestStop(s);
    if(WaitForSingleObject(s->done,5000)!=WAIT_OBJECT_0)s->error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    // No clear-on-timeout, forced unload, thread kill, or Explorer restart.
}

struct Watcher : winrt::implements<Watcher,IVisualTreeServiceCallback2,winrt::non_agile> {
    winrt::com_ptr<IXamlDiagnostics> diagnostics;
    std::weak_ptr<Session> session;
    std::mutex sourcesMutex;
    std::unordered_set<InstanceHandle> sources;
    Watcher(winrt::com_ptr<IUnknown> const& site,std::shared_ptr<Session> const& s):diagnostics(site.as<IXamlDiagnostics>()),session(s){}
    template<class T>T from(InstanceHandle h) {
        winrt::Windows::Foundation::IInspectable obj;
        winrt::check_hresult(diagnostics->GetIInspectableFromHandle(h,reinterpret_cast<::IInspectable**>(winrt::put_abi(obj))));
        return obj.as<T>();
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle,VisualElementState,LPCWSTR) noexcept override try {
        auto lifetime=get_strong();
        if(auto s=session.lock())trackedCallback(s->callbacks,[](bool){},[&]{s->error(winrt::to_hresult());});
        return S_OK;
    }catch(...){return S_OK;}
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation,VisualElement element,VisualMutationType mutation) noexcept override try {
        auto lifetime=get_strong();
        struct Strings {
            VisualElement& e;
            ~Strings(){SysFreeString(e.SrcInfo.FileName);SysFreeString(e.SrcInfo.Hash);SysFreeString(e.Name);SysFreeString(e.Type);}
        };
        auto s=session.lock();if(!s){Strings strings{element};return S_OK;}
        trackedCallback(s->callbacks,[&](bool admitted)->HRESULT {
        Strings strings{element};if(!admitted)return S_OK;
        if(mutation==Remove) {
            {std::lock_guard<std::mutex> lock(sourcesMutex);sources.erase(element.Handle);}
            // Lookup keeps ownership until this UI cleanup succeeds.
            if(auto old=s->life.lookup(element.Handle))restoreCurrent(s,old);
            return S_OK;
        }
        if(s->life.stopping)return S_OK;
        std::wstring_view type(element.Type?element.Type:L"",SysStringLen(element.Type));
        if(type==L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource") {
            std::lock_guard<std::mutex> lock(sourcesMutex);
            if(sources.size()<64)sources.insert(element.Handle);return S_OK;
        }
        if(type!=L"Windows.UI.Xaml.Shapes.Rectangle" || !element.Name)return S_OK;
        bool border=wcscmp(element.Name,L"BackgroundStroke")==0;
        if(!border && wcscmp(element.Name,L"BackgroundFill"))return S_OK;
        auto frame=from<ux::FrameworkElement>(relation.Parent);
        for(unsigned i=0;i<32 && frame && frame.Name()!=L"TaskbarFrame";++i)
            frame=ux::Media::VisualTreeHelper::GetParent(frame).try_as<ux::FrameworkElement>();
        if(!frame || frame.Name()!=L"TaskbarFrame" || winrt::get_class_name(frame)!=L"Taskbar.TaskbarFrame")return S_OK;
        auto root=ux::Media::VisualTreeHelper::GetParent(frame).try_as<ux::UIElement>();
        HWND island=nullptr;
        std::vector<InstanceHandle> candidates;
        {std::lock_guard<std::mutex> lock(sourcesMutex);candidates.assign(sources.begin(),sources.end());}
        for(auto h:candidates)try {
            auto source=from<ux::Hosting::DesktopWindowXamlSource>(h);
            if(source.Content()!=root)continue;
            winrt::check_hresult(source.as<IDesktopWindowXamlSourceNative>()->get_WindowHandle(&island));break;
        }catch(winrt::hresult_wrong_thread const&){}
        if(!island)return S_OK;
        HWND bar=GetAncestor(island,GA_PARENT);wchar_t cls[128]={};GetClassNameW(bar,cls,128);
        DWORD pid=0;GetWindowThreadProcessId(bar,&pid);
        if(pid!=s->state->targetPid || (wcscmp(cls,L"Shell_TrayWnd") && wcscmp(cls,L"Shell_SecondaryTrayWnd")))return S_OK;
        if(s->life.lookup(element.Handle))return S_OK;
        auto bg=std::make_shared<Background>();
        bg->handle=element.Handle;bg->island=island;bg->session=s;bg->uiThread=GetCurrentThreadId();bg->border=border;
        if(!s->life.begin(bg))return S_OK;
        BuildGuard guard{s,bg};
        bg->shape=from<ux::Shapes::Rectangle>(element.Handle);
        bg->original=bg->shape.Fill();bg->saved=true;
        // Actual XAML identity/geometry of the primary border, for the native
        // trial. Nothing is resized, repositioned or faded to remove the line.
        if(border && wcscmp(cls,L"Shell_TrayWnd")==0) {
            auto point=bg->shape.TransformToVisual(root).TransformPoint({0,0});
            auto xamlRoot=bg->shape.XamlRoot();winrt::check_hresult(interfaceResult(bool(xamlRoot)));
            s->state->strokeX=point.X;s->state->strokeY=point.Y;
            s->state->strokeWidth=bg->shape.ActualWidth();s->state->strokeHeight=bg->shape.ActualHeight();
            s->state->rasterScale=xamlRoot.RasterizationScale();
        }
        bg->queue=Queue::GetForCurrentThread();winrt::check_hresult(interfaceResult(bool(bg->queue)));
        // Transport copies can be released by the worker; actual XAML refs
        // are cleared only on UI. Refuse a non-agile dispatcher implementation.
        winrt::check_hresult(interfaceResult(bool(bg->queue.try_as<IAgileObject>())));
        ux::Media::SolidColorBrush clear;clear.Color(winrt::Windows::UI::Color{0,0,0,0});bg->clear=clear;
        std::weak_ptr<Background> weak=bg;
        bg->token=bg->shape.RegisterPropertyChangedCallback(ux::Shapes::Shape::FillProperty(),[weak](auto const&,auto const&){
            if(auto b=weak.lock()) {
                if(auto owner=b->session.lock())trackedCallback(owner->callbacks,[&](bool admitted) {
                    if(!admitted)return;
                    if(b->writingDepth || b->clean)return;
                    if(GetCurrentThreadId()!=b->uiThread)winrt::throw_hresult(RPC_E_WRONG_THREAD);
                    b->original=b->shape.Fill();b->saved=true;
                    if(owner->active && !owner->life.stopping)b->apply();
                },[&]{owner->error(winrt::to_hresult());});
            }
        });bg->registered=true;
        // Same mutex as stopping transition. A late registration is cleaned
        // HERE on its owning UI thread; it is never published or applied.
        if(!s->life.publish(bg)){s->life.abortBuild(bg);restoreCurrent(s,bg);guard.complete=true;return S_OK;}
        bg->published=true;InterlockedIncrement(&s->state->backgrounds);
        InterlockedIncrement(border?&s->state->strokes:&s->state->fills);
        s->primary.add(*s->state,bg->handle,reinterpret_cast<UINT_PTR>(bar),
            reinterpret_cast<UINT_PTR>(frame.as<IUnknown>().get()),border);guard.complete=true;
        if(s->active)bg->apply();return S_OK;
        },[&]{s->error(winrt::to_hresult());});
        return S_OK;
    }catch(...){return winrt::to_hresult();}
};

struct SubscriptionWork {
    std::shared_ptr<Session> s;
    winrt::com_ptr<IVisualTreeService3> tree;
    winrt::com_ptr<IVisualTreeServiceCallback2> watcher;
    HMODULE reference=nullptr;
};
static DWORD WINAPI subscriptionWorker(void* argument) {
    std::unique_ptr<SubscriptionWork> work(static_cast<SubscriptionWork*>(argument));
    HMODULE reference=work->reference;auto s=work->s;
    try {
        bool mayStart=!s->life.stopping && WaitForSingleObject(s->owner,0)==WAIT_TIMEOUT;
        if(!mayStart)requestStop(s);
        HRESULT hr=mayStart?work->tree->AdviseVisualTreeChange(work->watcher.get()):E_ABORT;
        if(mayStart)s->error(hr);
        bool advised=SUCCEEDED(hr);
        s->life.afterAdvise(advised,[&]{SetEvent(s->ready);});
        bool clean=true;
        if(advised) {
            HANDLE handles[]={s->owner,s->subscriptionStop};
            DWORD wait=WaitForMultipleObjects(2,handles,FALSE,INFINITE);
            if(wait!=WAIT_OBJECT_0+1)requestStop(s);
            // ONLY this owner calls Unadvise, after its own Advise returns.
            hr=work->tree->UnadviseVisualTreeChange(work->watcher.get());
            s->error(hr);clean=SUCCEEDED(hr);
        }
        // Unadvise is not treated as proof that already-entered callbacks ended.
        bool drained=s->callbacks.closeAndWait(std::chrono::seconds(5));
        if(!drained){s->error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));clean=false;}
        // Only verified agile diagnostics/service members are released here.
        // A failed assertion retains the work/root; never release on the wrong thread.
        bool releaseAllowed=s->releasePolicy.allows(GetCurrentThreadId());
        if(!releaseAllowed){s->error(RPC_E_WRONG_THREAD);clean=false;}
        if(clean) {
            work->watcher=nullptr;work->tree=nullptr;
            {std::lock_guard<std::mutex> lock(s->life.mutex);s->watcher=nullptr;s->tree=nullptr;}
        }
        s->life.finishSubscription(clean);tryFinish(s);
    }catch(...){s->error(winrt::to_hresult());s->life.finishSubscription(false);}
    if(!s->releasePolicy.allows(GetCurrentThreadId()))work.release();
    work.reset();s.reset();
    FreeLibraryAndExitThread(reference,0);
}
struct Site : winrt::implements<Site,IObjectWithSite,winrt::non_agile> {
    winrt::com_ptr<IUnknown> site;
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid,void** out) noexcept override{return site.as(iid,out);}
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* value) noexcept override try {
        if(!value){site=nullptr;return S_OK;}
        std::shared_ptr<Session> s;
        {std::lock_guard<std::mutex> lock(sessionMutex);s=current.lock();}
        if(!s || WaitForSingleObject(s->owner,0)!=WAIT_TIMEOUT || !s->life.claimSubscription())return E_ABORT;
        try {
            winrt::com_ptr<IUnknown> candidate;candidate.copy_from(value);
            // Refuse an unverified external COM object's cross-thread lifetime.
            winrt::check_hresult(interfaceResult(bool(candidate.try_as<IAgileObject>())));
            auto tree=candidate.as<IVisualTreeService3>();
            winrt::check_hresult(interfaceResult(bool(tree.try_as<IAgileObject>())));
            auto diagnostics=candidate.as<IXamlDiagnostics>();
            winrt::check_hresult(interfaceResult(bool(diagnostics.try_as<IAgileObject>())));
            site=candidate;
            auto watcher=winrt::make_self<Watcher>(site,s);
            // Our watcher contains agile diagnostics plus mutex-protected plain
            // data/weak refs; it holds no UI-affine shapes. Its internal refcount
            // releases are permitted here after callback drain.
            s->releasePolicy={GetCurrentThreadId(),true};
            auto work=std::make_unique<SubscriptionWork>();
            work->s=s;work->tree=tree;work->watcher=watcher.as<IVisualTreeServiceCallback2>();
            {std::lock_guard<std::mutex> lock(s->life.mutex);s->watcher=work->watcher;s->tree=tree;}
            winrt::check_bool(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&subscriptionWorker),&work->reference));
            HANDLE thread=CreateThread(nullptr,0,subscriptionWorker,work.get(),0,nullptr);
            if(!thread){DWORD error=GetLastError();FreeLibrary(work->reference);winrt::throw_hresult(HRESULT_FROM_WIN32(error));}
            work.release();CloseHandle(thread);return S_OK;
        }catch(...) {
            HRESULT hr=winrt::to_hresult();s->error(hr);
            {std::lock_guard<std::mutex> lock(s->life.mutex);s->watcher=nullptr;s->tree=nullptr;}
            s->life.finishSubscription(true);SetEvent(s->ready);tryFinish(s);return hr;
        }
    }catch(...){return winrt::to_hresult();}
};
struct Factory : winrt::implements<Factory,IClassFactory> {
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override{return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) noexcept override try {
        if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;
        return winrt::make_self<Site>().as(iid,out);
    }catch(...){return winrt::to_hresult();}
};
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void** out) try {
    if(!out)return E_POINTER;*out=nullptr;if(clsid!=TapClass)return CLASS_E_CLASSNOTAVAILABLE;
    return winrt::make_self<Factory>().as(iid,out);
}catch(...){return winrt::to_hresult();}
extern "C" HRESULT __stdcall DllCanUnloadNow(){return S_FALSE;}

static DWORD workerRun() noexcept try {
    auto s=std::make_shared<Session>();DWORD pid=GetCurrentProcessId();
    s->mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,objectName(pid,L"state").c_str());
    if(!s->mapping)return ERROR_INVALID_HANDLE;
    s->state=static_cast<State*>(MapViewOfFile(s->mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(State)));
    if(!s->state || s->state->magic!=ProtocolMagic || s->state->version!=ProtocolVersion || s->state->targetPid!=pid)return ERROR_INVALID_DATA;
    wchar_t image[MAX_PATH]={},windows[MAX_PATH]={};GetModuleFileNameW(nullptr,image,MAX_PATH);GetWindowsDirectoryW(windows,MAX_PATH);
    std::wstring expected=std::wstring(windows)+L"\\explorer.exe";
    DWORD actualPid=0;GetWindowThreadProcessId(reinterpret_cast<HWND>(s->state->taskbar),&actualPid);
    if(_wcsicmp(image,expected.c_str()) || actualPid!=pid)return ERROR_ACCESS_DENIED;
    s->owner=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,s->state->ownerPid);
    FILETIME created,e,k,u;
    if(!s->owner || !GetProcessTimes(s->owner,&created,&e,&k,&u) || fileTicks(created)!=s->state->ownerCreation)return ERROR_ACCESS_DENIED;
    auto event=[&](wchar_t const* suffix){return OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,objectName(pid,suffix).c_str());};
    s->ready=event(L"ready");s->command=event(L"command");s->appliedEvent=event(L"applied");s->done=event(L"done");
    s->subscriptionStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!s->ready || !s->command || !s->appliedEvent || !s->done || !s->subscriptionStop)return ERROR_INVALID_HANDLE;
    // Pending UI lambdas and retained failure state can outlive BOTH workers.
    // Use our own documented process-lifetime pin, never XAML's incidental pin
    // or DllCanUnloadNow. Normal exit restores UI but does not unload this DLL.
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&workerRun),&pinned))return GetLastError();
    s->retention=new std::shared_ptr<Session>(s);
    struct Cleanup {
        std::shared_ptr<Session> s;bool complete=false;
        ~Cleanup(){if(!complete)try{stop(s);}catch(...){s->error(E_FAIL);}}
    } cleanup{s};
    // A quit does not reset the process-wide XAML diagnostics state. Never call
    // initialization for a second controller session in the same Explorer.
    if(!initialization.begin()) {
        HRESULT error=HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        s->error(error);SetEvent(s->ready);stop(s);cleanup.complete=true;return DWORD(error);
    }
    {std::lock_guard<std::mutex> lock(sessionMutex);current=s;}
    HMODULE xaml=LoadLibraryExW(L"Windows.UI.Xaml.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto init=xaml?reinterpret_cast<decltype(&InitializeXamlDiagnosticsEx)>(GetProcAddress(xaml,"InitializeXamlDiagnosticsEx")):nullptr;
    HRESULT hr=init?E_FAIL:HRESULT_FROM_WIN32(GetLastError());
    wchar_t path[MAX_PATH]={};GetModuleFileNameW(module,path,MAX_PATH);
    if(init && !s->life.stopping && WaitForSingleObject(s->owner,0)==WAIT_TIMEOUT) {
        // Upstream's fresh-thread diagnostics cache workaround. A hung API is
        // not preempted: the host times out; owner checks prevent any late apply.
        // No second endpoint attempt after a partial failure either.
        std::thread([&]{hr=init(L"VisualDiagConnection1",pid,nullptr,path,TapClass,nullptr);}).join();
    }
    if(xaml)FreeLibrary(xaml);
    if(FAILED(hr)){s->error(hr);SetEvent(s->ready);stop(s);cleanup.complete=true;return DWORD(hr);}
    HANDLE handles[]={s->owner,s->command};
    DWORD lifetime=s->state->maxLifetimeMs<90000?s->state->maxLifetimeMs:90000;
    ULONGLONG deadline=GetTickCount64()+lifetime;
    while(!s->life.stopping) {
        ULONGLONG now=GetTickCount64();DWORD timeout=!lifetime?INFINITE:(now>=deadline?0:DWORD(deadline-now));
        DWORD result=WaitForMultipleObjects(2,handles,FALSE,timeout);
        if(result!=WAIT_OBJECT_0+1)break;
        LONG command=InterlockedExchange(&s->state->command,CommandNone);
        if(command==CommandStop)break;
        if(command!=CommandApply)continue;
        auto batch=s->life.prepareApply([&](size_t count){
            s->active=true;ResetEvent(s->appliedEvent);
            InterlockedExchange(&s->state->applyPending,LONG(count));
        });
        if(!batch.allowed)break;
        if(batch.entries.empty())s->error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        dispatch(s,batch.entries,false);
    }
    stop(s);cleanup.complete=true;return 0;
}catch(...){return DWORD(winrt::to_hresult());}
static DWORD WINAPI worker(void* reference) {
    DWORD result=workerRun();running=false;
    FreeLibraryAndExitThread(static_cast<HMODULE>(reference),result);
}
extern "C" LRESULT CALLBACK TinyTaskbarHook(int code,WPARAM wp,LPARAM lp) noexcept {
    if(code>=0 && lp) {
        auto message=reinterpret_cast<CWPSTRUCT*>(lp);
        if(message->message==RegisterWindowMessageW(BeginMessage)) {
            bool expected=false;
            if(running.compare_exchange_strong(expected,true)) {
                HMODULE reference=nullptr;
                if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&TinyTaskbarHook),&reference)) {
                    HANDLE thread=CreateThread(nullptr,0,worker,reference,0,nullptr);
                    if(thread)CloseHandle(thread);else{FreeLibrary(reference);running=false;}
                }else running=false;
            }
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,void*) {
    if(reason==DLL_PROCESS_ATTACH)module=instance;return TRUE;
}
