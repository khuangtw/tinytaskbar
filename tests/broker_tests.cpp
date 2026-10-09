// SPDX-License-Identifier: GPL-3.0-only
// Deterministic tests of the production BrokerLifecycle and callback accounting.
// Mock brush writes model UI success/failure; they do not validate XAML/Explorer.
#include "broker_lifecycle.hpp"
#include "session_safety.hpp"
#include <cstdio>
#include <future>
#include <thread>
#include <stdexcept>
#include <atomic>
static void expect(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
struct Shape {uint64_t handle=1;int original=7,paint=7;bool throws=false;};
using Life=BrokerLifecycle<Shape>;
static bool finish(Life& life){CallbackDrain callbacks;return life.finishRestore(callbacks);}
static ClientIdentity first{101,1001},second{102,1002};
static void ready(Life& life){expect(life.beginInitialization() && life.claimSubscription(),"First initialization/subscription rejected.");life.initialized(true);}
static std::shared_ptr<Shape> add(Life& life,uint64_t handle=1){auto shape=std::make_shared<Shape>();shape->handle=handle;expect(life.begin(shape) && life.publish(shape),"Shape registration rejected.");return shape;}
static void apply(Life& life,Life::Batch const& batch,ClientIdentity owner){for(auto const& shape:batch.entries){if(life.mayApply(batch.generation,owner))shape->paint=0;life.workFinished();}}
static void restore(Life& life,Life::Batch const& batch){for(auto const& shape:batch.entries){if(!shape->throws){shape->paint=shape->original;life.restored(shape);}else life.fail();life.workFinished();}}
static void sequential(){
    Life life;ready(life);auto shape=add(life);
    expect(life.attach(first,true,true),"First attach.");auto one=life.apply(first);apply(life,one,first);expect(shape->paint==0,"First apply.");
    restore(life,life.restore(first));expect(finish(life) && shape->paint==7,"First quit restore.");
    expect(!life.beginInitialization() && !life.claimSubscription(),"Reconnect attempted initialization/subscription twice.");
    expect(life.attach(second,true,true) && life.snapshot().generation==2,"Second controller did not reconnect.");
    apply(life,life.apply(second),second);restore(life,life.restore(second));expect(finish(life) && shape->paint==7,"Second restore.");
}
static void abrupt(){Life life;ready(life);auto shape=add(life);expect(life.attach(first,true,true),"Attach.");apply(life,life.apply(first),first);restore(life,life.restore(first,true));expect(finish(life) && shape->paint==7 && !life.snapshot().owner,"Owner death did not restore and release ownership.");expect(life.attach(second,true,true),"Owner death blocked clean reconnect.");}
static void parallel(){
    Life life;ready(life);std::promise<void> release;auto barrier=release.get_future().share();std::atomic<unsigned> wins{0};
    std::thread a([&]{barrier.wait();if(life.attach(first,true,true))++wins;});std::thread b([&]{barrier.wait();if(life.attach(second,true,true))++wins;});
    release.set_value();a.join();b.join();expect(wins==1 && life.snapshot().mode==BrokerMode::Attached,"Parallel clients both acquired ownership.");
}
static void stale(){Life life;ready(life);auto shape=add(life);expect(life.attach(first,true,true),"Attach.");auto old=life.apply(first);auto stop=life.restore(first);expect(!life.mayApply(old.generation,first),"Apply admitted after stop.");restore(life,stop);expect(!finish(life),"Stop acknowledged before old queued apply drained.");apply(life,old,first);expect(shape->paint==7 && finish(life),"Old apply overwrote restoration.");expect(life.attach(second,true,true),"Reconnect.");expect(!life.mayApply(old.generation,second) && !life.mayApply(old.generation,first),"Old epoch crossed reconnect.");}
static void gone(){Life life;ready(life);add(life);expect(life.attach(first,true,true),"Attach.");auto queued=life.apply(first);life.explorerGone();expect(!life.mayApply(queued.generation,first) && !life.attach(second,true,true) && !life.beginInitialization(),"Gone Explorer accepted work/reinit.");Life replacement;ready(replacement);expect(replacement.attach(second,true,true),"New Explorer lacks independent session.");}
static void initFailure(){Life life;expect(life.beginInitialization(),"Initial attempt.");life.initialized(false);expect(life.snapshot().mode==BrokerMode::Poisoned && !life.beginInitialization() && !life.claimSubscription() && !life.attach(first,true,true),"Failed initialization was retried.");}
static void mismatch(){Life life;ready(life);expect(!life.attach(first,false,true) && !life.attach(first,true,false),"Version mismatch/dead owner acquired broker.");expect(life.attach(first,true,true),"Refusal damaged valid attach.");auto epoch=life.snapshot().generation;ClientIdentity reused{first.pid,first.birth+1};expect(!life.apply(reused).allowed && !life.restore(reused).allowed && !life.mayApply(epoch,reused),"Reused PID acquired old identity.");}
static void drain(){
    Life life;ready(life);auto shape=add(life);expect(life.attach(first,true,true),"Attach.");apply(life,life.apply(first),first);
    CallbackDrain callbacks;std::promise<void> entered,release;auto barrier=release.get_future().share();
    std::thread callback([&]{trackedCallback(callbacks,[&](bool allowed){expect(allowed,"Resident callback closed.");entered.set_value();barrier.wait();},[]{});});entered.get_future().wait();
    restore(life,life.restore(first));expect(shape->paint==7 && !life.finishRestore(callbacks) && !life.attach(second,true,true),"Restore acknowledged while entered callback retained state.");
    release.set_value();callback.join();expect(life.finishRestore(callbacks) && life.attach(second,true,true),"Quiescent callbacks prevented reconnect.");
    trackedCallback(callbacks,[&](bool allowed){expect(allowed,"Disconnect permanently closed resident callbacks.");},[]{});
}
static void restoreFailure(){Life life;ready(life);auto shape=add(life);expect(life.attach(first,true,true),"Attach.");apply(life,life.apply(first),first);shape->throws=true;restore(life,life.restore(first));expect(!finish(life) && life.snapshot().poisoned && !life.attach(second,true,true),"Failed brush restoration acknowledged or reconnect admitted.");shape->throws=false;shape->paint=shape->original;life.restored(shape);expect(finish(life) && life.snapshot().mode==BrokerMode::Poisoned && !life.attach(second,true,true),"Poisoned state became reusable after late restore.");}
static void building(){Life life;ready(life);expect(life.attach(first,true,true),"Attach.");auto shape=std::make_shared<Shape>();expect(life.begin(shape),"Begin build.");auto stop=life.restore(first);restore(life,stop);expect(!finish(life),"Unpublished UI ownership ignored by drain.");expect(life.publish(shape),"Inactive resident registration rejected.");expect(finish(life) && life.attach(second,true,true),"Late inactive registration did not drain.");apply(life,life.apply(second),second);expect(shape->paint==0,"Retained late element absent on reconnect.");}
static void removed(){Life life;ready(life);auto shape=add(life);expect(life.attach(first,true,true),"Attach.");auto stop=life.restore(first);expect(life.removed(shape),"Removed UI element retained in live set.");restore(life,stop);expect(finish(life),"Removed/queued element could not drain.");expect(life.attach(second,true,true) && life.apply(second).entries.empty(),"Removed element reused by new owner.");}
static void rejectedQueue(){Life life;ready(life);auto shape=add(life);expect(life.attach(first,true,true),"Attach.");apply(life,life.apply(first),first);auto stop=life.restore(first);expect(stop.entries.size()==1,"Stop snapshot.");life.fail();life.workFinished();expect(shape->paint==0 && !finish(life) && !life.attach(second,true,true),"TryEnqueue failure discarded saved ownership or acknowledged restore.");expect(life.lookup(shape->handle)==shape,"Failed queue lost original brush ownership.");}
static void inactiveTheme(){Life life;ready(life);auto shape=add(life);expect(life.attach(first,true,true),"Attach.");apply(life,life.apply(first),first);restore(life,life.restore(first));expect(finish(life),"Restore.");shape->original=12;shape->paint=12;expect(life.attach(second,true,true),"Reconnect.");apply(life,life.apply(second),second);restore(life,life.restore(second));expect(finish(life) && shape->paint==12,"Inactive theme brush was not retained by next session.");}
static void partialBuildFailure(){Life life;ready(life);auto shape=std::make_shared<Shape>();expect(life.begin(shape),"Build ownership.");life.abortBuild(shape);life.fail();auto stop=life.restore({},false,true);restore(life,stop);expect(!finish(life) && life.lookup(shape->handle)==shape,"Unpublished failed UI cleanup was falsely acknowledged.");life.removed(shape);expect(finish(life) && life.snapshot().mode==BrokerMode::Poisoned,"Late UI cleanup did not finish permanent poison safely.");}
static void ownerWait(){Life life;ready(life);expect(!life.watchesOwner(),"Inactive broker watched an owner.");expect(life.attach(first,true,true) && life.watchesOwner(),"Attached owner not watched.");auto shape=add(life);auto stop=life.restore(first,true);expect(!life.watchesOwner(),"Permanently signalled dead owner would cause restore spin.");restore(life,stop);expect(finish(life) && !life.watchesOwner(),"Restored broker watched a stale owner.");}
static void callbackReporting(){CallbackDrain callbacks;std::promise<void> entered,release;auto barrier=release.get_future().share();std::thread callback([&]{trackedCallback(callbacks,[](bool){throw std::runtime_error("callback");},[&]{entered.set_value();barrier.wait();});});entered.get_future().wait();expect(!callbacks.quiescent(),"Error-reporting callback ceased to be counted.");release.set_value();callback.join();expect(callbacks.quiescent(),"Error-reporting ticket leaked.");}
static void callbackTransitionGap(){
    Life life;ready(life);expect(life.attach(first,true,true),"Attach.");life.restore(first);
    CallbackDrain callbacks;bool stale=callbacks.quiescent();
    std::promise<void> entered,release;auto barrier=release.get_future().share();
    std::thread callback([&]{CallbackDrain::Scope scope(callbacks);entered.set_value();barrier.wait();});
    entered.get_future().wait();bool actual=callbacks.quiescent();
    bool finished=life.finishRestore(callbacks),reconnected=life.attach(second,true,true);
    release.set_value();callback.join();
    expect(stale && !actual && !finished && !reconnected,"Snapshot gap allowed Idle/reconnect while a callback was entered.");
    expect(life.finishRestore(callbacks) && life.attach(second,true,true),"Quiescent transition did not allow later reconnect.");
    printf("REGRESSION stale_quiescent=%d actual_quiescent=%d finish_restore=%d reconnect=%d\n",stale,actual,finished,reconnected);
}
int main()try{
    struct Test{char const* name;void(*run)();};Test tests[]={
        {"two_controllers_one_initialization",sequential},{"abrupt_client_death_restore_reconnect",abrupt},{"parallel_start_single_owner",parallel},
        {"stale_apply_fenced_and_stop_waits_pending",stale},{"explorer_gone_new_session_independent",gone},{"initialization_failure_permanent_refusal",initFailure},
        {"version_mismatch_dead_owner_pid_reuse",mismatch},{"callback_drain_before_reconnect",drain},{"restore_failure_permanent_poison",restoreFailure},
        {"registration_during_stop_stays_inactive",building},{"removed_element_not_reused",removed},{"enqueue_failure_retains_brush",rejectedQueue},{"inactive_theme_original_retained",inactiveTheme},{"partial_build_failure_blocks_false_ack",partialBuildFailure},
        {"dead_owner_excluded_from_restore_wait",ownerWait},{"callback_exception_reporting_counted",callbackReporting},{"callback_admission_restore_transition_same_lock",callbackTransitionGap}};
    for(auto const& test:tests){test.run();printf("PASS %s\n",test.name);}printf("17 broker CPU groups passed. No Explorer, GUI or XAML load.\n");return 0;
}catch(std::exception const& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
