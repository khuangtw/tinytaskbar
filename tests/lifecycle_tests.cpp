// GPL-3.0. Exercises the DLL's actual Lifecycle helper without Windows/XAML.
// Barriers force the review's problematic interleavings; no timed sleeps.
#include "lifecycle.hpp"
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>

static void require(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
struct MockBackground {
    unsigned long long handle=1;
    std::thread::id ui=std::this_thread::get_id();
    bool savedBrush=true,registered=true,shape=true,fillThrows=false;
    void restore() {
        require(std::this_thread::get_id()==ui,"wrong-thread cleanup");
        registered=false;
        if(fillThrows)throw std::runtime_error("injected Fill failure");
        savedBrush=false;shape=false;
    }
};
using Life=Lifecycle<MockBackground>;
static bool ack(Life& life,int& done,bool drained=true) {
    return life.acknowledge([&]{return drained;},[&]{++done;});
}
static void lateRegistration() {
    Life life;int done=0;std::promise<void> registered,release;
    auto go=release.get_future().share();std::exception_ptr failure;
    std::thread ui([&]{try {
        auto bg=std::make_shared<MockBackground>();require(life.begin(bg),"begin");
        registered.set_value();go.wait();
        require(!life.publish(bg),"late publication admitted");
        bg->restore();require(!life.cleaned(bg),"late bg counted as published");
    }catch(...){failure=std::current_exception();}});
    registered.get_future().wait();
    auto batch=life.beginStop([](size_t){});
    require(batch.allowed && batch.entries.empty(),"stop saw unpublished entry");
    require(life.retained()==1 && !ack(life,done),"done before late UI cleanup");
    release.set_value();ui.join();if(failure)std::rethrow_exception(failure);
    require(life.retained()==0 && ack(life,done) && done==1,"late cleanup not acknowledged");
    require(!ack(life,done) && done==1,"duplicate done");
}
static void publishedSnapshotAndPendingQueue() {
    Life life;int done=0;
    auto bg=std::make_shared<MockBackground>();require(life.begin(bg) && life.publish(bg),"publish");
    auto batch=life.beginStop([](size_t count){require(count==1,"snapshot count");});
    require(batch.entries.size()==1 && !ack(life,done),"published background omitted");
    bg->restore();require(life.cleaned(bg),"published cleanup count");
    require(!ack(life,done,false),"done before accepted queue callback drained");
    require(ack(life,done) && done==1,"queue completion failed");
    require(!life.prepareApply([](size_t){}).allowed,"apply admitted after stop");
    require(!life.begin(std::make_shared<MockBackground>()),"new build admitted after stop");
}
static void enqueueFailureRetainsBrush() {
    Life life;int done=0;std::weak_ptr<MockBackground> saved;
    {
        auto bg=std::make_shared<MockBackground>();saved=bg;
        require(life.begin(bg) && life.publish(bg),"publish");
        auto batch=life.beginStop([](size_t){});
        // TryEnqueue=false: no cleaned() call. The production DLL does the same.
        batch.entries.clear();bg.reset();
    }
    require(!saved.expired() && life.retained()==1 && !ack(life,done),"queue failure discarded original");
    auto bg=saved.lock();require(bg->savedBrush && bg->registered && bg->shape,"cleanup state changed off UI");
    bg->restore();require(life.cleaned(bg),"retry cleanup");
    require(ack(life,done),"successful later UI cleanup cannot finish");
}
static void restoreExceptionRetainsBrush() {
    Life life;int done=0;auto bg=std::make_shared<MockBackground>();
    require(life.begin(bg) && life.publish(bg),"publish");life.beginStop([](size_t){});
    bg->fillThrows=true;bool fault=false;
    try{bg->restore();life.cleaned(bg);}catch(std::runtime_error const&){fault=true;}
    require(fault && bg->savedBrush && bg->shape && !bg->registered,"failed Fill lost saved refs");
    require(life.retained()==1 && !ack(life,done),"failed restore acknowledged");
    bg->fillThrows=false;bg->restore();require(life.cleaned(bg) && ack(life,done),"idempotent restore retry failed");
}
static void incompleteBuildFailure() {
    Life life;int done=0;auto bg=std::make_shared<MockBackground>();
    require(life.begin(bg),"begin");life.beginStop([](size_t){});life.abortBuild(bg);
    bg->fillThrows=true;try{bg->restore();}catch(std::runtime_error const&){}
    require(life.retained()==1 && !ack(life,done),"failed late cleanup acknowledged");
    bg->fillThrows=false;bg->restore();require(!life.cleaned(bg) && ack(life,done),"late build cleanup");
}
static void lateAdviseHasOneOwner() {
    Life life;int done=0,ready=0,advise=0,unadvise=0;
    require(life.claimSubscription(),"claim");require(!life.claimSubscription(),"duplicate subscription owner");
    std::promise<void> adviseEntered,adviseReturn,unadviseEntered,unadviseReturn;
    auto advised=adviseReturn.get_future().share(),unadvised=unadviseReturn.get_future().share();
    std::thread owner([&]{
        ++advise;adviseEntered.set_value();advised.wait();
        life.afterAdvise(true,[&]{++ready;});
        ++unadvise;unadviseEntered.set_value();unadvised.wait();
        life.finishSubscription(true);
    });
    adviseEntered.get_future().wait();life.beginStop([](size_t){});
    require(!ack(life,done),"done while Advise pending");
    adviseReturn.set_value();unadviseEntered.get_future().wait();
    require(!ack(life,done),"done while late Unadvise pending");
    unadviseReturn.set_value();owner.join();
    require(ready==0 && advise==1 && unadvise==1,"subscription owner or late ready race");
    require(ack(life,done) && done==1,"serialized subscription cleanup");
}
static void subscriptionFailuresBlockFalseDone() {
    Life failed;int done=0,ready=0;
    require(failed.claimSubscription(),"claim");failed.afterAdvise(true,[&]{++ready;});
    failed.beginStop([](size_t){});failed.finishSubscription(false);
    require(ready==1 && failed.subscriptionStatus()==Life::Subscription::Failed && !ack(failed,done),"failed Unadvise acknowledged");
    Life initFailed;require(initFailed.claimSubscription(),"claim");
    initFailed.afterAdvise(false,[]{});initFailed.beginStop([](size_t){});
    require(!ack(initFailed,done),"failed Advise context claimed clean before owner released refs");
    initFailed.finishSubscription(true);require(ack(initFailed,done),"failed Advise clean shutdown");
}
static void normalSubscriptionAndCleanup() {
    Life life;int done=0,ready=0;
    require(life.claimSubscription(),"claim");life.afterAdvise(true,[&]{++ready;});
    auto bg=std::make_shared<MockBackground>();require(life.begin(bg) && life.publish(bg),"publish");
    size_t pending=0;auto apply=life.prepareApply([&](size_t count){pending=count;});
    require(apply.allowed && apply.entries.size()==1 && pending==1 && ready==1,"normal readiness/apply");
    life.beginStop([](size_t){});bg->restore();require(life.cleaned(bg),"normal restore");
    require(!ack(life,done),"done before Unadvise");life.finishSubscription(true);
    require(ack(life,done) && done==1,"normal stop");
}
static void fillAndStrokeRestoreIndependently() {
    Life life;int done=0;
    auto fill=std::make_shared<MockBackground>(),stroke=std::make_shared<MockBackground>();stroke->handle=2;
    require(life.begin(fill) && life.publish(fill) && life.begin(stroke) && life.publish(stroke),"publish fill and stroke");
    auto batch=life.beginStop([](size_t n){require(n==2,"stop omitted one brush");});
    require(batch.entries.size()==2,"both brushes must be in stop snapshot");
    fill->restore();require(life.cleaned(fill),"fill restoration");
    stroke->fillThrows=true;try{stroke->restore();}catch(std::runtime_error const&){}
    require(stroke->savedBrush && stroke->shape && life.retained()==1 && !ack(life,done),"failed border discarded original or false done");
    stroke->fillThrows=false;stroke->restore();require(life.cleaned(stroke) && ack(life,done),"border retry cleanup");
}
int main() try {
    struct Test {char const* name;void(*run)();};
    Test tests[]={
        {"stop_during_registration",lateRegistration},
        {"published_snapshot_and_pending_queue",publishedSnapshotAndPendingQueue},
        {"enqueue_failure_retains_original",enqueueFailureRetainsBrush},
        {"restore_exception_retains_original",restoreExceptionRetainsBrush},
        {"late_build_failure_retains_original",incompleteBuildFailure},
        {"late_advise_serialized_unadvise",lateAdviseHasOneOwner},
        {"subscription_failures_no_false_done",subscriptionFailuresBlockFalseDone},
        {"normal_subscription_and_cleanup",normalSubscriptionAndCleanup},
        {"fill_and_stroke_cleanup_independent",fillAndStrokeRestoreIndependently}
    };
    for(auto const& test:tests){test.run();printf("PASS %s\n",test.name);}
    printf("9 deterministic lifecycle/fault cases passed; no Explorer or XAML load.\n");return 0;
}catch(std::exception const& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
