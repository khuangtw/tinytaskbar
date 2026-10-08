// GPL-3.0-only; modified 2026-10-07/08. See ../LICENSE and ../THIRD_PARTY.md.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>

inline constexpr GUID TapClass = {0x2cb3b83d,0x3a46,0x45bf,{0xb3,0x46,0xc8,0x41,0x3c,0x77,0x92,0x23}};
inline constexpr DWORD ProtocolMagic = 0x54425459, ProtocolVersion = 3;
inline constexpr wchar_t BeginMessage[] = L"TinyTaskbar.Begin.2cb3b83d-3a46-45bf-b346-c8413c779223";
enum : LONG { CommandNone=0, CommandApply=1, CommandStop=2 };
enum : LONG { PrimaryFillLive=1,PrimaryStrokeLive=2,PrimaryPairLive=4,PrimaryPairApplied=8 };
struct State {
    DWORD magic, version, targetPid, ownerPid;
    ULONGLONG ownerCreation;
    UINT_PTR taskbar;
    DWORD maxLifetimeMs; // Bounded probe uses 90000; normal manual-run mode may use 0 with owner-death restoration.
    volatile LONG command, error, backgrounds, applied, restored, applyPending, restorePending, stopping;
    volatile LONG cleanupComplete, cleanupRetained;
    volatile LONG fills,strokes,appliedFills,appliedStrokes,restoredFills,restoredStrokes;
    volatile LONG primaryReadiness; // Uses the existing 4-byte padding; cumulative counts remain unchanged.
    double strokeX,strokeY,strokeWidth,strokeHeight,rasterScale;
};
inline std::wstring objectName(DWORD pid,const wchar_t* suffix) {
    return L"Local\\TinyTaskbar.2cb3b83d-v3."+std::to_wstring(pid)+L"."+suffix;
}
inline ULONGLONG fileTicks(FILETIME t) {return (ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
inline void recordError(State* s,HRESULT hr) {if(s && FAILED(hr))InterlockedCompareExchange(&s->error,LONG(hr),0);}
