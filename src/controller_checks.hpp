// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "protocol.hpp"
#include <cstring>
inline bool compatibleState(State const& state,DWORD pid,ULONGLONG birth){return state.magic==ProtocolMagic && state.version==ProtocolVersion && state.bytes==sizeof(State) && state.build==BrokerBuild && std::memcmp(state.revision,BrokerRevision,32)==0 && state.targetPid==pid && state.targetCreation==birth;}
inline bool compatibleRequest(State const& state){return state.requestVersion==ProtocolVersion && std::memcmp(state.requestRevision,BrokerRevision,32)==0;}
inline bool primaryReady(State const& state,bool applied=false){LONG flags=readLong(state.primaryReadiness);constexpr LONG live=PrimaryFillLive|PrimaryStrokeLive|PrimaryPairLive;return !readLong(state.error) && (flags&live)==live && (!applied || (flags&PrimaryPairApplied));}
enum class DllGate {Unrelated,Current,OlderOrOtherVersion};
inline DllGate classifyDll(wchar_t const* name){
    if(_wcsicmp(name,L"ExplorerTAP.dll")==0)return DllGate::OlderOrOtherVersion;
    if(_wcsnicmp(name,L"TinyTaskbarTap",14))return DllGate::Unrelated;
    return _wcsicmp(name,L"TinyTaskbarTap-v4.dll")?DllGate::OlderOrOtherVersion:DllGate::Current;
}
