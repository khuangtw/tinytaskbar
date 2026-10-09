// GPL-3.0. Live primary pair metadata only; owns no XAML references.
#pragma once
#include "protocol.hpp"
#include <mutex>
#include <vector>
#include <algorithm>

class PrimaryPair {
    struct Entry { ULONGLONG handle; UINT_PTR frame; bool stroke,applied=false; };
    std::mutex mutex;
    std::vector<Entry> live;
    void publishLocked(State& state) {
        LONG flags=0;
        for(auto const& item:live) {
            flags|=item.stroke?PrimaryStrokeLive:PrimaryFillLive;
            if(!item.stroke)for(auto const& stroke:live)if(stroke.stroke && stroke.frame==item.frame) {
                flags|=PrimaryPairLive;
                if(item.applied && stroke.applied)flags|=PrimaryPairApplied;
            }
        }
        InterlockedExchange(&state.primaryReadiness,flags);
    }
public:
    void rebind(State& state,UINT_PTR taskbar) {
        std::lock_guard<std::mutex> lock(mutex);
        if(state.taskbar!=taskbar){state.taskbar=taskbar;live.clear();publishLocked(state);}
    }
    void clearApplied(State& state) {
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& item:live)item.applied=false;
        publishLocked(state);
    }
    void add(State& state,ULONGLONG handle,UINT_PTR taskbar,UINT_PTR frame,bool stroke) {
        std::lock_guard<std::mutex> lock(mutex);
        if(taskbar!=state.taskbar || !frame)return;
        if(std::none_of(live.begin(),live.end(),[&](auto const& e){return e.handle==handle;}))
            live.push_back({handle,frame,stroke});
        publishLocked(state);
    }
    void applied(State& state,ULONGLONG handle) {
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& e:live)if(e.handle==handle)e.applied=true;
        publishLocked(state);
    }
    void remove(State& state,ULONGLONG handle) {
        std::lock_guard<std::mutex> lock(mutex);
        live.erase(std::remove_if(live.begin(),live.end(),[&](auto const& e){return e.handle==handle;}),live.end());
        publishLocked(state);
    }
};
