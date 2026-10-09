// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "protocol.hpp"
#include <stdexcept>
#include <vector>
struct OwnedHandle {
    HANDLE value=nullptr;
    OwnedHandle()=default;
    OwnedHandle(OwnedHandle const&)=delete;
    OwnedHandle& operator=(OwnedHandle const&)=delete;
    ~OwnedHandle(){reset();}
    void reset(HANDLE next=nullptr){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=next;}
};
inline void require(bool ok,char const* reason){if(!ok)throw std::runtime_error(reason);}
struct OwnedMutex : OwnedHandle {
    bool owns=false;
    ~OwnedMutex(){if(owns && value)ReleaseMutex(value);}
};
inline void claimMutex(OwnedMutex& mutex,std::wstring const& name){
    mutex.value=CreateMutexW(nullptr,TRUE,name.c_str());DWORD error=GetLastError();
    require(mutex.value!=nullptr,"Cannot create controller ownership mutex.");
    if(error!=ERROR_ALREADY_EXISTS)mutex.owns=true;
    else{DWORD result=WaitForSingleObject(mutex.value,0);mutex.owns=result==WAIT_OBJECT_0 || result==WAIT_ABANDONED;}
    require(mutex.owns,"Another controller owns this Explorer/session.");
}
