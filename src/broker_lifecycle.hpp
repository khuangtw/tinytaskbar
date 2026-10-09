// SPDX-License-Identifier: GPL-3.0-only
// Production broker state machine. No Windows/XAML dependencies.
#pragma once
#include <cstdint>
#include <mutex>
#include <memory>
#include <vector>
#include <algorithm>
#include "session_safety.hpp"

struct ClientIdentity {
    uint32_t pid=0;
    uint64_t birth=0;
    bool operator==(ClientIdentity const& other) const noexcept {return pid==other.pid && birth==other.birth;}
    explicit operator bool() const noexcept {return pid!=0 && birth!=0;}
};
enum class BrokerMode { Starting, Idle, Attached, Restoring, Poisoned, Gone };
template<class T> class BrokerLifecycle {
public:
    using Ptr=std::shared_ptr<T>;
    struct Batch {bool allowed=false;uint64_t generation=0;std::vector<Ptr> entries;};
    struct Snapshot {BrokerMode mode;ClientIdentity owner;uint64_t generation;bool active,poisoned;size_t pending;};
private:
    struct Entry {Ptr item;bool building=true,published=false,restoreNeeded=false;};
    mutable std::mutex mutex;
    BrokerMode mode=BrokerMode::Starting;
    bool attempted=false,subscriptionClaimed=false,poisoned=false,active=false;
    ClientIdentity owner{};
    uint64_t generation=0;
    size_t pending=0;
    std::vector<Entry> owned;
    auto find(Ptr const& p){return std::find_if(owned.begin(),owned.end(),[&](auto const& e){return e.item==p;});}
public:
    bool beginInitialization(){std::lock_guard<std::mutex> lock(mutex);if(attempted || mode!=BrokerMode::Starting)return false;attempted=true;return true;}
    bool claimSubscription(){std::lock_guard<std::mutex> lock(mutex);if(!attempted || subscriptionClaimed || poisoned || mode!=BrokerMode::Starting)return false;subscriptionClaimed=true;return true;}
    void initialized(bool success){std::lock_guard<std::mutex> lock(mutex);if(mode!=BrokerMode::Starting)return;if(success && !poisoned)mode=BrokerMode::Idle;else {poisoned=true;mode=BrokerMode::Poisoned;}}
    bool attach(ClientIdentity client,bool compatible,bool alive){
        std::lock_guard<std::mutex> lock(mutex);
        if(mode!=BrokerMode::Idle || poisoned || !compatible || !alive || !client || pending)return false;
        owner=client;++generation;active=false;mode=BrokerMode::Attached;return true;
    }
    Batch apply(ClientIdentity client){
        std::lock_guard<std::mutex> lock(mutex);if(mode!=BrokerMode::Attached || poisoned || !(owner==client))return {};
        active=true;Batch result{true,generation,{}};
        for(auto const& e:owned)if(e.published)result.entries.push_back(e.item);
        pending+=result.entries.size();return result;
    }
    // Tickets are counted BEFORE TryEnqueue; rejected/throwing queues must end them.
    bool mayApply(uint64_t epoch,ClientIdentity client) const {
        std::lock_guard<std::mutex> lock(mutex);return mode==BrokerMode::Attached && active && !poisoned && generation==epoch && owner==client;
    }
    Batch restore(ClientIdentity client,bool ownerDeath=false,bool failure=false){
        std::lock_guard<std::mutex> lock(mutex);
        if(failure)poisoned=true;
        if(mode==BrokerMode::Gone || mode==BrokerMode::Restoring)return {};
        if(!failure && (mode!=BrokerMode::Attached || !(owner==client)))return {};
        (void)ownerDeath;active=false;mode=BrokerMode::Restoring;
        Batch result{true,generation,{}};
        for(auto& e:owned)if(e.published){e.restoreNeeded=true;result.entries.push_back(e.item);}
        pending+=result.entries.size();return result;
    }
    void restored(Ptr const& p){std::lock_guard<std::mutex> lock(mutex);auto it=find(p);if(it!=owned.end())it->restoreNeeded=false;}
    void workFinished(){std::lock_guard<std::mutex> lock(mutex);if(pending)--pending;else {poisoned=true;active=false;}}
    void fail(){std::lock_guard<std::mutex> lock(mutex);poisoned=true;active=false;}
    bool finishRestore(CallbackDrain& callbacks){
        return callbacks.whenQuiescent([&]{
        std::lock_guard<std::mutex> lock(mutex);
        if(mode!=BrokerMode::Restoring || pending)return false;
        for(auto const& e:owned)if(e.building || e.restoreNeeded)return false;
        active=false;owner={};mode=poisoned?BrokerMode::Poisoned:BrokerMode::Idle;return true;
        });
    }
    void explorerGone(){std::lock_guard<std::mutex> lock(mutex);active=false;owner={};mode=BrokerMode::Gone;}
    Snapshot snapshot() const {std::lock_guard<std::mutex> lock(mutex);return {mode,owner,generation,active,poisoned,pending};}
    bool watchesOwner() const {std::lock_guard<std::mutex> lock(mutex);return mode==BrokerMode::Attached && !poisoned;}
    bool begin(Ptr const& p){std::lock_guard<std::mutex> lock(mutex);if(poisoned || mode==BrokerMode::Gone)return false;owned.push_back({p,true,false,false});return true;}
    bool publish(Ptr const& p){std::lock_guard<std::mutex> lock(mutex);auto it=find(p);if(it==owned.end())return false;it->building=false;if(poisoned || mode==BrokerMode::Gone)return false;it->published=true;return true;}
    void abortBuild(Ptr const& p){std::lock_guard<std::mutex> lock(mutex);auto it=find(p);if(it!=owned.end()){it->building=false;if(!it->published)it->restoreNeeded=true;}}
    Ptr lookup(uint64_t handle){std::lock_guard<std::mutex> lock(mutex);for(auto const& e:owned)if(e.item->handle==handle)return e.item;return {};}
    // Only after all XAML members/callback tokens have been released on their UI thread.
    bool removed(Ptr const& p){std::lock_guard<std::mutex> lock(mutex);auto it=find(p);if(it==owned.end())return false;bool published=it->published;owned.erase(it);return published;}
};
