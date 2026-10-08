// GPL-3.0. Bounded trial startup wait; no GUI input-idle heuristic.
#pragma once
#include "dll_identity.hpp"

enum class AppliedWait { Applied,ChildExited,Timeout,Failed };
template<class Open> AppliedWait waitApplied(HANDLE child,DWORD milliseconds,Open open) {
    OwnedHandle applied;
    ULONGLONG end=GetTickCount64()+milliseconds;
    for(;;) {
        DWORD status=WaitForSingleObject(child,0);
        if(status==WAIT_OBJECT_0)return AppliedWait::ChildExited;
        if(status!=WAIT_TIMEOUT)return AppliedWait::Failed;
        ULONGLONG now=GetTickCount64();
        if(now>=end)return AppliedWait::Timeout;
        if(!applied.value)applied.value=open();
        now=GetTickCount64();if(now>=end)return AppliedWait::Timeout;
        DWORD remaining=DWORD(end-now);
        // IPC may not exist yet. Wait on child termination between bounded
        // creation retries; once opened, wait only for the applied event/child.
        HANDLE waits[]={child,applied.value};
        DWORD delay=applied.value?remaining:(remaining<50?remaining:50);
        DWORD result=WaitForMultipleObjects(applied.value?2:1,waits,FALSE,delay);
        if(result==WAIT_OBJECT_0)return AppliedWait::ChildExited;
        if(applied.value && result==WAIT_OBJECT_0+1) {
            status=WaitForSingleObject(child,0);
            return status==WAIT_TIMEOUT?AppliedWait::Applied:
                   status==WAIT_OBJECT_0?AppliedWait::ChildExited:AppliedWait::Failed;
        }
        if(result!=WAIT_TIMEOUT)return AppliedWait::Failed;
        if(applied.value)return AppliedWait::Timeout;
    }
}
