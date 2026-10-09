// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include "broker_build_identity.hpp"

// Incompatible with v3. A loaded older/different image is refused before injection.
inline constexpr GUID TapClass={0x4eda63e0,0x7e91,0x4ec0,{0x9f,0xc2,0xd4,0xcb,0x44,0x7e,0x04,0x01}};
inline constexpr DWORD ProtocolMagic=0x54425459,ProtocolVersion=4;
inline constexpr ULONGLONG BrokerBuild=0x202610080001ULL;
inline constexpr wchar_t BeginMessage[]=L"TinyTaskbar.Begin.4eda63e0-7e91-4ec0-9fc2-d4cb447e0401";
enum : LONG {CommandNone=0,CommandAttach=1,CommandApply=2,CommandStop=3};
enum : LONG {StatusStarting=0,StatusIdle=1,StatusAttached=2,StatusRestoring=3,StatusPoisoned=4};
enum : LONG {PrimaryFillLive=1,PrimaryStrokeLive=2,PrimaryPairLive=4,PrimaryPairApplied=8};
struct State {
    DWORD magic,version,bytes,targetPid;
    ULONGLONG targetCreation,build;
    unsigned char revision[32];
    UINT_PTR taskbar;
    DWORD bootstrapPid,reserved;
    ULONGLONG bootstrapCreation;
    // Request publication is sequence-last (InterlockedExchange); one controller
    // holds the per-Explorer client mutex. Broker never writes request payload.
    DWORD requestPid,requestVersion;
    ULONGLONG requestCreation;
    unsigned char requestRevision[32];
    volatile LONG command,requestSequence,responseSequence,responseError;
    DWORD ownerPid,ownerReserved;
    ULONGLONG ownerCreation,generation;
    volatile LONG status,error,backgrounds,applied,restored,applyPending,restorePending;
    volatile LONG cleanupComplete,cleanupRetained;
    volatile LONG fills,strokes,appliedFills,appliedStrokes,restoredFills,restoredStrokes;
    volatile LONG primaryReadiness,initializations;
    double strokeX,strokeY,strokeWidth,strokeHeight,rasterScale;
};
inline ULONGLONG fileTicks(FILETIME value){return (ULONGLONG(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
inline std::wstring objectName(DWORD pid,ULONGLONG birth,wchar_t const* suffix){return L"Local\\TinyTaskbar.4eda63e0-v4."+std::to_wstring(pid)+L"."+std::to_wstring(birth)+L"."+suffix;}
inline LONG readLong(volatile LONG const& value){return InterlockedCompareExchange(const_cast<volatile LONG*>(&value),0,0);}
inline void recordError(State* state,HRESULT hr){if(state && FAILED(hr))InterlockedCompareExchange(&state->error,LONG(hr),0);}
