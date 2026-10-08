// SPDX-License-Identifier: GPL-3.0-only
// Offline shared-helper tests, not an Explorer/XAML substitute.
#include "session_safety.hpp"
#include "protocol.hpp"
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/base.h>
#include <cstdio>
#include <future>
#include <thread>
#include <stdexcept>
#include <memory>
#include <vector>
#include <string>
static void expect(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
static void sessions() {
    InitializationGate explorer;unsigned initializations=0;
    auto start=[&]{if(!explorer.begin())return false;++initializations;return true;};
    expect(start(),"First session refused.");
    // Normal stop/owner death do not reset the process-wide diagnostics gate.
    expect(!start() && initializations==1,"Second session reinitialized diagnostics.");
    InitializationGate anotherExplorer;expect(anotherExplorer.begin(),"New Explorer did not get its own gate.");
    printf("PASS two_sessions_initialize_once_and_new_explorer_is_independent\n");
}
static void blockedCallback() {
    CallbackDrain callbacks;std::promise<void> entered,unblock,waiting;
    auto release=unblock.get_future().share();bool result=false,refsReleased=false;
    std::thread callback([&]{CallbackDrain::Scope scope(callbacks);entered.set_value();release.wait();});
    entered.get_future().wait();callbacks.close();
    std::thread owner([&]{waiting.set_value();result=callbacks.closeAndWait(std::chrono::seconds(5));if(result)refsReleased=true;});
    waiting.get_future().wait();bool held=!callbacks.drained();
    unblock.set_value();callback.join();owner.join();
    expect(held && result && refsReleased && callbacks.drained(),"References released before an active callback completed.");
    {CallbackDrain::Scope late(callbacks);expect(!late,"Late callback admitted after close.");expect(!callbacks.drained(),"Entered rejected callback was not counted.");}
    expect(callbacks.drained(),"Late callback ticket did not drain.");
    printf("PASS blocked_callback_prevents_release_and_late_callback_is_rejected\n");
}
static void timeoutAndThrow() {
    CallbackDrain callbacks;
    {CallbackDrain::Scope scope(callbacks);expect(!callbacks.closeAndWait(std::chrono::milliseconds(0)),"A blocked callback reported successful drain.");}
    expect(callbacks.drained(),"Callback did not drain after timeout.");
    CallbackDrain failing;
    try{CallbackDrain::Scope scope(failing);throw std::runtime_error("injected callback failure");}catch(std::runtime_error const&){}
    expect(failing.closeAndWait(std::chrono::milliseconds(0)),"Exception leaked an active callback count.");
    printf("PASS drain_timeout_retains_ownership_and_exception_balances_count\n");
}
struct Shape {
    std::vector<std::string>& order;
    explicit Shape(std::vector<std::string>& events):order(events){}
    ~Shape(){order.push_back("shape_release");}
};
static void nestedWrites() {
    unsigned depth=0;std::vector<std::string> order;
    auto shape=std::make_shared<Shape>(order);bool outerProtected=false;
    {
        auto callReceiver=shape;WriteDepth outer(depth);expect(depth==1,"Outer write not tracked.");
        {WriteDepth nested(depth);expect(depth==2,"Nested restoring Fill not tracked.");
            // Model a nested Remove clearing the saved member while Fill is active.
            shape.reset();order.push_back("remove_members");expect(order.size()==1,"Active Fill lost its receiver.");}
        outerProtected=depth==1;order.push_back("fill_return");
    }
    expect(depth==0 && outerProtected && order==std::vector<std::string>{"remove_members","fill_return","shape_release"},"Nested callback reset the outer write or released its receiver before Fill returned.");
    try{WriteDepth write(depth);throw std::runtime_error("injected Fill failure");}catch(std::runtime_error const&){}
    expect(depth==0,"Fill exception left a writing flag set.");
    bool restoring=false,cleaned=false;
    {RestoreGuard outer(restoring);expect(bool(outer),"Outer restore refused.");
        {RestoreGuard nested(restoring);if(nested)cleaned=true;}
        expect(restoring && !cleaned,"Nested Remove acknowledged incomplete restoration.");}
    expect(!restoring,"Restore guard was not reset.");
    printf("PASS nested_fill_remove_preserves_outer_depth_and_receiver_release_order\n");
}
static void releaseAffinity() {
    ComReleasePolicy nonAgile{100,false};bool released=false;
    if(nonAgile.allows(200))released=true;
    expect(!released && nonAgile.allows(100),"Unverified COM reference released on another thread.");
    ComReleasePolicy agile{100,true};expect(agile.allows(200),"Verified agile release refused.");
    CallbackDrain callbacks;std::vector<std::string> order;
    {CallbackDrain::Scope scope(callbacks);callbacks.close();
        if(callbacks.drained() && agile.allows(200))order.push_back("release");
        expect(order.empty(),"COM released before callback completion.");order.push_back("callback_return");}
    expect(callbacks.closeAndWait(std::chrono::milliseconds(0)),"Final callback did not drain.");
    if(agile.allows(200))order.push_back("release");
    expect(order==std::vector<std::string>{"callback_return","release"},"COM release order incorrect.");
    printf("PASS_com_release_affinity_and_callback_release_order\n");
}
static void exceptionReportingLifetime() {
    CallbackDrain callbacks;std::promise<void> handling,resume;auto proceed=resume.get_future().share();
    std::atomic<bool> reported{false};auto watcher=std::make_shared<int>(1);std::weak_ptr<int> weak=watcher;
    std::thread callback([&,self=watcher]{
        // Same production helper used by both visual-tree/property callbacks.
        trackedCallback(callbacks,[](bool){throw std::runtime_error("injected work failure");},[&]{handling.set_value();proceed.wait();reported=true;});
    });watcher.reset();handling.get_future().wait();
    bool premature=callbacks.closeAndWait(std::chrono::milliseconds(0));bool alive=!weak.expired();
    resume.set_value();callback.join();
    expect(!premature && alive && reported && callbacks.drained() && weak.expired(),"Drain/release completed while exception reporting was still executing.");
    printf("PASS production_callback_exception_handler_remains_counted_and_alive\n");
    CallbackDrain failingHandler;
    try{trackedCallback(failingHandler,[](bool){throw std::runtime_error("body");},[]{throw std::runtime_error("handler");});}catch(std::runtime_error const&){}
    expect(failingHandler.closeAndWait(std::chrono::milliseconds(0)),"Throwing error handler leaked its active ticket.");
    printf("PASS throwing_exception_handler_balances_ticket\n");
}
static void deterministicInterfaceFailure() {
    for(DWORD lastError:{DWORD(ERROR_SUCCESS),DWORD(ERROR_ACCESS_DENIED)}) {
        State state={};SetLastError(lastError);HRESULT caught=S_OK;
        try{winrt::check_hresult(interfaceResult(false));}catch(...){caught=winrt::to_hresult();recordError(&state,caught);}
        expect(caught==E_NOINTERFACE && state.error==E_NOINTERFACE && FAILED(caught),"Missing interface used stale Win32 last-error or returned success.");
        winrt::check_hresult(interfaceResult(true));
    }
    printf("PASS missing_interface_fixed_hresult_with_zero_and_nonzero_last_error\n");
}
int main() try {
    sessions();blockedCallback();timeoutAndThrow();nestedWrites();releaseAffinity();exceptionReportingLifetime();deterministicInterfaceFailure();
    printf("8 session safety groups passed; no Explorer, GUI or XAML/DLL load.\n");return 0;
}catch(std::exception const& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
