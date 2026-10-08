// GPL-3.0. Offline regressions: real wait handles and live-pair metadata.
// Child lifetime uses a waitable event stand-in; no GUI, Explorer or DLL load.
#include "applied_wait.hpp"
#include "primary_pair.hpp"
#include "controller_checks.hpp"
#include <cstdio>
#include <cstddef>

static_assert(sizeof(State)==144 && offsetof(State,primaryReadiness)==100 &&
              offsetof(State,strokeX)==104,"Existing IPC size/geometry offsets must not move.");
struct WaitFixture {
    OwnedHandle child,applied,earlyIdle;
    WaitFixture() {
        child.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        applied.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        earlyIdle.value=CreateEventW(nullptr,TRUE,TRUE,nullptr);
        require(child.value && applied.value && earlyIdle.value,"Cannot create offline wait handles.");
    }
    HANDLE open() {
        HANDLE result=nullptr;
        require(DuplicateHandle(GetCurrentProcess(),applied.value,GetCurrentProcess(),&result,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,"Duplicate event handle.");
        return result;
    }
};
static void startupWaits() {
    {WaitFixture f;
        require(waitApplied(f.child.value,25,[&]{return f.open();})==AppliedWait::Timeout,"Unrelated early-idle signal completed an unsignaled applied wait.");
        printf("PASS early_idle_cannot_complete_unsignaled_applied_wait\n");}
    {WaitFixture f;unsigned opens=0;SetEvent(f.applied.value);
        require(waitApplied(f.child.value,500,[&]{return ++opens==1?nullptr:f.open();})==AppliedWait::Applied && opens==2,"Delayed IPC creation/apply signal.");
        printf("PASS delayed_ipc_creation_then_applied_event\n");}
    {WaitFixture f;
        require(waitApplied(f.child.value,25,[]{return HANDLE(nullptr);})==AppliedWait::Timeout,"Absent IPC did not respect deadline.");
        printf("PASS absent_ipc_deadline\n");}
    {WaitFixture f;unsigned opens=0;SetEvent(f.child.value);SetEvent(f.applied.value);
        require(waitApplied(f.child.value,500,[&]{++opens;return f.open();})==AppliedWait::ChildExited && opens==0,"Exited child/applied signal was accepted.");
        printf("PASS child_exit_before_ipc_has_priority\n");}
    {WaitFixture f;
        require(waitApplied(f.child.value,500,[&]{auto event=f.open();SetEvent(f.child.value);return event;})==AppliedWait::ChildExited,"Child exit during applied wait was missed.");
        printf("PASS child_exit_during_applied_wait\n");}
    require(waitApplied(INVALID_HANDLE_VALUE,500,[]{return HANDLE(nullptr);})==AppliedWait::Failed,"Invalid wait handle was accepted.");
    printf("PASS failed_wait_is_not_ready\n");
}
static void primaryPairs() {
    State state={};state.taskbar=100;state.fills=8;state.strokes=8;
    state.appliedFills=8;state.appliedStrokes=8;
    PrimaryPair pair;
    pair.add(state,1,100,200,false);pair.add(state,9,101,200,true);
    require(applyGate(state,false)==ElementGate::MissingStroke,"Secondary stroke masked missing primary stroke.");
    printf("PASS secondary_stroke_cannot_mask_missing_primary_stroke\n");
    pair.add(state,2,100,201,true);
    require(applyGate(state,false)==ElementGate::MissingPair,"Different TaskbarFrame elements formed a false pair.");
    printf("PASS cross_frame_elements_cannot_form_primary_pair\n");
    pair.remove(state,2);pair.add(state,2,100,200,true);
    require(applyGate(state,false)==ElementGate::Ready && applyGate(state,false,true)==ElementGate::NotApplied,"Live pair/apply gate.");
    pair.applied(state,9);pair.applied(state,1);
    require(applyGate(state,false,true)==ElementGate::NotApplied,"Cumulative/secondary apply counts masked unapplied primary stroke.");
    pair.applied(state,2);require(applyGate(state,false,true)==ElementGate::Ready,"Applied live primary pair refused.");
    printf("PASS only_applied_live_primary_pair_completes_startup\n");
    pair.remove(state,2);
    require(applyGate(state,false)==ElementGate::MissingStroke && state.fills==8 && state.strokes==8,"Removal/cumulative restoration separation failed.");
    pair.applied(state,2);require(applyGate(state,false,true)==ElementGate::MissingStroke,"Late apply revived removed primary stroke.");
    printf("PASS removal_and_late_apply_cannot_reuse_cumulative_counts\n");
    pair.add(state,2,100,200,true);
    require(applyGate(state,false,true)==ElementGate::NotApplied,"Recreated element inherited old applied status.");
    printf("PASS recreated_element_requires_new_apply\n");
}
int wmain() try {
    startupWaits();primaryPairs();
    printf("11 P2 regressions passed; no Explorer, GUI or XAML/DLL load.\n");return 0;
}catch(std::exception const& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
