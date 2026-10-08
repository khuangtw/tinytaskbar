// GPL-3.0. Small shared gates used by the real controller and offline checks.
#pragma once
#include "protocol.hpp"
enum class ElementGate { Ready, Cancelled, CallbackFailure, MissingFill, MissingStroke, MissingPair, NotApplied };
inline ElementGate applyGate(State const& state,bool quitRequested,bool requireApplied=false) {
    if(quitRequested || state.stopping)return ElementGate::Cancelled;
    if(state.error)return ElementGate::CallbackFailure;
    LONG primary=state.primaryReadiness;
    if(!(primary&PrimaryFillLive))return ElementGate::MissingFill;
    if(!(primary&PrimaryStrokeLive))return ElementGate::MissingStroke;
    if(!(primary&PrimaryPairLive))return ElementGate::MissingPair;
    if(requireApplied && !(primary&PrimaryPairApplied))return ElementGate::NotApplied;
    return ElementGate::Ready;
}
enum class DllGate { Unrelated, Current, OlderOrOtherVersion };
inline DllGate classifyDll(wchar_t const* name) {
    if(_wcsnicmp(name,L"TinyTaskbarTap-",15))return DllGate::Unrelated;
    return _wcsicmp(name,L"TinyTaskbarTap-v3.dll")?DllGate::OlderOrOtherVersion:DllGate::Current;
}
