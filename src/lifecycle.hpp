// GPL-3.0. Shared by the actual DLL and deterministic, non-XAML fault tests.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>

template<class T> class Lifecycle {
public:
    using Ptr=std::shared_ptr<T>;
    enum class Subscription { Idle, Starting, Subscribed, Finished, Failed };
    struct Batch { bool allowed=false; std::vector<Ptr> entries; };
    std::mutex mutex;
    std::atomic<bool> stopping{false};
private:
    struct Entry { Ptr item; bool building=true, published=false; };
    std::vector<Entry> owned;
    Subscription subscription=Subscription::Idle;
    bool acknowledged=false;
    auto find(Ptr const& p) {return std::find_if(owned.begin(),owned.end(),[&](auto const& e){return e.item==p;});}
    std::vector<Ptr> publishedLocked() const {
        std::vector<Ptr> result;
        for(auto const& e:owned)if(e.published)result.push_back(e.item);
        return result;
    }
public:
    // Insert ownership BEFORE acquiring any XAML references or registering a callback.
    bool begin(Ptr const& p) {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopping)return false;
        owned.push_back({p,true,false});return true;
    }
    bool publish(Ptr const& p) {
        std::lock_guard<std::mutex> lock(mutex);auto it=find(p);
        if(it==owned.end())return false;
        it->building=false;
        if(stopping)return false;
        it->published=true;return true;
    }
    void abortBuild(Ptr const& p) {
        std::lock_guard<std::mutex> lock(mutex);auto it=find(p);
        if(it!=owned.end())it->building=false;
    }
    Ptr lookup(unsigned long long handle) {
        std::lock_guard<std::mutex> lock(mutex);
        for(auto const& e:owned)if(e.published && e.item->handle==handle)return e.item;
        return {};
    }
    // Call only AFTER successful cleanup on the owning UI thread.
    bool cleaned(Ptr const& p) {
        std::lock_guard<std::mutex> lock(mutex);auto it=find(p);
        if(it==owned.end())return false;
        bool published=it->published;owned.erase(it);return published;
    }
    template<class F> Batch prepareApply(F prepare) {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopping)return {};
        Batch batch{true,publishedLocked()};prepare(batch.entries.size());return batch;
    }
    template<class F> Batch beginStop(F prepare) {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopping.exchange(true))return {};
        Batch batch{true,publishedLocked()};prepare(batch.entries.size());return batch;
    }
    bool claimSubscription() {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopping || subscription!=Subscription::Idle)return false;
        subscription=Subscription::Starting;return true;
    }
    template<class F> void afterAdvise(bool success,F signalReady) {
        std::lock_guard<std::mutex> lock(mutex);
        subscription=success?Subscription::Subscribed:Subscription::Starting;
        // Ready publication shares the stop lock: no successful late readiness.
        if(!stopping)signalReady();
    }
    void finishSubscription(bool clean) {
        std::lock_guard<std::mutex> lock(mutex);
        subscription=clean?Subscription::Finished:Subscription::Failed;
    }
    template<class P,class F> bool acknowledge(P workDrained,F signalDone) {
        std::lock_guard<std::mutex> lock(mutex);
        bool subscriptionClean=subscription==Subscription::Idle || subscription==Subscription::Finished;
        if(acknowledged || !stopping || !owned.empty() || !subscriptionClean || !workDrained())return false;
        acknowledged=true;signalDone();return true;
    }
    size_t retained() {std::lock_guard<std::mutex> lock(mutex);return owned.size();}
    Subscription subscriptionStatus() {std::lock_guard<std::mutex> lock(mutex);return subscription;}
};
