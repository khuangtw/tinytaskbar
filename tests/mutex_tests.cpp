// SPDX-License-Identifier: GPL-3.0-only
// Kernel mutex fixtures in our own PID namespace; no desktop/Explorer access.
#include "win32_helpers.hpp"
#include <future>
#include <thread>
#include <cstdio>
static std::wstring name(wchar_t const* suffix){return L"Local\\TinyTaskbar.CpuFixture.4eda63e0."+std::to_wstring(GetCurrentProcessId())+L"."+suffix;}
static void exclusiveAndRelease(){
    auto fixture=name(L"exclusive");std::promise<void> entered,release,released,exitThread;auto go=release.get_future().share(),exit=exitThread.get_future().share();std::exception_ptr failure;
    std::thread owner([&]{try{{OwnedMutex lock;claimMutex(lock,fixture);entered.set_value();go.wait();}released.set_value();exit.wait();}catch(...){failure=std::current_exception();}});
    entered.get_future().wait();OwnedHandle observer;observer.value=OpenMutexW(SYNCHRONIZE,FALSE,fixture.c_str());bool refused=false;
    try{OwnedMutex other;claimMutex(other,fixture);}catch(std::runtime_error const&){refused=true;}
    release.set_value();released.get_future().wait();bool acquired=false;try{OwnedMutex replacement;claimMutex(replacement,fixture);acquired=true;}catch(...){}
    exitThread.set_value();owner.join();if(failure)std::rethrow_exception(failure);
    require(observer.value && refused && acquired,"Exclusive/RAII release failed while previous thread was still alive.");
    printf("PASS real_mutex_parallel_refusal_and_release_before_thread_exit\n");
}
static void abandoned(){
    auto fixture=name(L"abandoned");std::promise<void> entered,release;auto go=release.get_future().share();
    std::thread owner([&]{OwnedHandle raw;raw.value=CreateMutexW(nullptr,TRUE,fixture.c_str());entered.set_value();go.wait();});
    entered.get_future().wait();OwnedHandle observer;observer.value=OpenMutexW(SYNCHRONIZE,FALSE,fixture.c_str());release.set_value();owner.join();
    require(observer.value!=nullptr,"Could not retain abandoned fixture.");OwnedMutex replacement;claimMutex(replacement,fixture);require(replacement.owns,"Abandoned dead controller mutex not reclaimed.");printf("PASS abandoned_owner_can_be_reclaimed\n");
}
static void collision(){auto fixture=name(L"collision");OwnedHandle event;event.value=CreateEventW(nullptr,FALSE,FALSE,fixture.c_str());require(event.value!=nullptr,"Event fixture.");bool refused=false;try{OwnedMutex lock;claimMutex(lock,fixture);}catch(std::runtime_error const&){refused=true;}require(refused,"Different named object was overwritten.");printf("PASS named_object_type_collision_refused\n");}
static void birth(){auto a=objectName(11,101,L"client"),b=objectName(11,102,L"client");require(a!=b,"PID reuse shares client namespace.");OwnedMutex first,second;claimMutex(first,a+L".cpu."+std::to_wstring(GetCurrentProcessId()));claimMutex(second,b+L".cpu."+std::to_wstring(GetCurrentProcessId()));require(first.owns && second.owns,"Independent Explorer births share mutex.");printf("PASS different_explorer_births_have_independent_mutexes\n");}
int main()try{exclusiveAndRelease();abandoned();collision();birth();printf("4 Win32 CPU fixture groups passed. No Explorer, GUI or XAML load.\n");return 0;}catch(std::exception const& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
