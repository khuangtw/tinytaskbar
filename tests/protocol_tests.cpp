// SPDX-License-Identifier: GPL-3.0-only
// Production protocol/image classifiers, no live desktop/process enumeration.
#include "controller_checks.hpp"
#include <cstdio>
#include <stdexcept>
static void expect(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
static State state(){State s={};s.magic=ProtocolMagic;s.version=ProtocolVersion;s.bytes=sizeof(State);s.build=BrokerBuild;s.targetPid=55;s.targetCreation=100;std::memcpy(s.revision,BrokerRevision,32);s.requestVersion=ProtocolVersion;std::memcpy(s.requestRevision,BrokerRevision,32);return s;}
int main()try{
    auto valid=state();expect(compatibleState(valid,55,100) && compatibleRequest(valid),"Valid identity refused.");printf("PASS matching_broker_and_request_identity\n");
    for(unsigned field=0;field<4;++field){auto changed=state();if(field==0)changed.magic^=1;if(field==1)--changed.version;if(field==2)--changed.bytes;if(field==3)++changed.build;expect(!compatibleState(changed,55,100),"Malformed header accepted.");}printf("PASS incompatible_header_refused\n");
    auto changed=state();changed.revision[7]^=1;expect(!compatibleState(changed,55,100),"Source revision mismatch accepted.");expect(!compatibleState(valid,56,100) && !compatibleState(valid,55,101),"Target PID reuse accepted.");printf("PASS_source_revision_and_explorer_birth_refused\n");
    changed=state();--changed.requestVersion;expect(!compatibleRequest(changed),"Foreign command version accepted.");changed=state();changed.requestRevision[0]^=1;expect(!compatibleRequest(changed),"Foreign command source accepted.");printf("PASS_foreign_request_revision_refused\n");
    for(auto name:{L"TinyTaskbarTap-v2.dll",L"TinyTaskbarTap-v3.dll",L"TinyTaskbarTap.dll",L"TinyTaskbarTap-v5.dll",L"ExplorerTAP.dll"})expect(classifyDll(name)==DllGate::OlderOrOtherVersion,"Another diagnostics image accepted.");expect(classifyDll(L"tinytaskbartap-v4.DLL")==DllGate::Current && classifyDll(L"Windows.UI.Xaml.dll")==DllGate::Unrelated,"DLL classifier invalid.");printf("PASS_loaded_image_version_gate\n");
    valid.primaryReadiness=PrimaryPairLive;expect(!primaryReady(valid),"Missing live brushes accepted.");valid.primaryReadiness|=PrimaryFillLive|PrimaryStrokeLive;expect(primaryReady(valid) && !primaryReady(valid,true),"Incomplete apply accepted.");valid.primaryReadiness|=PrimaryPairApplied;expect(primaryReady(valid,true),"Live apply refused.");valid.error=E_FAIL;expect(!primaryReady(valid,true),"Poisoned readiness accepted.");printf("PASS_primary_apply_gate_and_error_refusal\n");
    printf("6 protocol CPU groups passed. No Explorer, GUI or XAML load.\n");return 0;
}catch(std::exception const& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
