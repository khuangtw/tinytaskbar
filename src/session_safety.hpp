// SPDX-License-Identifier: GPL-3.0-only
// Shared production helpers; tests use the same initialization and drain gates.
#pragma once
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdint>

class InitializationGate {
    std::atomic<bool> attempted{false};
public:
    bool begin() noexcept {bool unused=false;return attempted.compare_exchange_strong(unused,true);}
};
class CallbackDrain {
    std::mutex mutex;
    std::condition_variable changed;
    unsigned active=0;
    bool closed=false;
    void leave(){std::lock_guard<std::mutex> lock(mutex);if(--active==0)changed.notify_all();}
public:
    class Scope {
        CallbackDrain* owner;
        bool allowed;
    public:
        Scope(CallbackDrain& drain):owner(&drain) {
            std::lock_guard<std::mutex> lock(drain.mutex);++drain.active;allowed=!drain.closed;
        }
        Scope(Scope const&)=delete;
        Scope& operator=(Scope const&)=delete;
        ~Scope(){owner->leave();}
        explicit operator bool() const noexcept{return allowed;}
    };
    void close(){std::lock_guard<std::mutex> lock(mutex);closed=true;}
    bool closeAndWait(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);closed=true;
        return changed.wait_for(lock,timeout,[&]{return active==0;});
    }
    bool drained(){std::lock_guard<std::mutex> lock(mutex);return closed && active==0;}
};
// The active ticket outlives exception reporting, not just the work body.
// Rejected late callbacks still run their bounded housekeeping with admitted=false.
template<class Body,class Failure>
void trackedCallback(CallbackDrain& drain,Body body,Failure failure) {
    CallbackDrain::Scope callback(drain);
    try{body(bool(callback));}catch(...){failure();}
}
// QI/object presence is not a Win32 last-error operation.
inline constexpr int32_t interfaceResult(bool present) noexcept {
    return present?0:-2147467262; // E_NOINTERFACE (0x80004002).
}
// An external COM reference can be released here only on its acquiring thread,
// or after explicit IAgileObject verification. Wrong-thread release is retained.
struct ComReleasePolicy {
    uint32_t acquiringThread=0;
    bool verifiedAgile=false;
    bool allows(uint32_t currentThread) const noexcept {
        return verifiedAgile || currentThread==acquiringThread;
    }
};
class WriteDepth {
    unsigned& depth;
public:
    explicit WriteDepth(unsigned& value):depth(value){++depth;}
    WriteDepth(WriteDepth const&)=delete;
    WriteDepth& operator=(WriteDepth const&)=delete;
    ~WriteDepth(){--depth;}
};
class RestoreGuard {
    bool& restoring;
    bool owns=false;
public:
    explicit RestoreGuard(bool& flag):restoring(flag){if(!flag){flag=true;owns=true;}}
    RestoreGuard(RestoreGuard const&)=delete;
    RestoreGuard& operator=(RestoreGuard const&)=delete;
    ~RestoreGuard(){if(owns)restoring=false;}
    explicit operator bool() const noexcept{return owns;}
};
