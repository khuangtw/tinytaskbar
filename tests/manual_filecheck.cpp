// GPL-3.0. No DLL loading, Explorer access or desktop mutation.
#include "dll_identity.hpp"
#include "controller_checks.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>

static_assert(sizeof(State)==144 && ProtocolVersion==3,"v3 IPC layout.");
int wmain(int argc,wchar_t** argv) try {
    require(argc==2,"Pass the frozen DLL path.");
    {TrustedDllFile actual(argv[1]);printf("PASS exact_reviewed_dll_sha256_and_size\n");}
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<char> copy((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    require(copy.size()==size_t(ExpectedDllBytes),"Cannot read test fixture.");copy[256]^=1;
    wchar_t directory[MAX_PATH]={},fixture[MAX_PATH]={};
    require(GetTempPathW(MAX_PATH,directory)>0 && GetTempFileNameW(directory,L"tbt",0,fixture)!=0,"Cannot create temp fixture.");
    struct Fixture {wchar_t const* path;~Fixture(){DeleteFileW(path);}} cleanup{fixture};
    {std::ofstream output(fixture,std::ios::binary);output.write(copy.data(),std::streamsize(copy.size()));require(bool(output),"Cannot write altered fixture.");}
    bool rejected=false;
    try{TrustedDllFile changed(fixture);}catch(std::runtime_error const&){rejected=true;}
    require(rejected,"Altered same-size DLL was accepted.");printf("PASS altered_same_size_file_rejected\n");
    std::wstring missing=std::wstring(fixture)+L".missing";rejected=false;
    try{TrustedDllFile absent(missing);}catch(std::runtime_error const&){rejected=true;}
    require(rejected,"Missing DLL was accepted.");printf("PASS missing_file_rejected\n");
    State state={};state.primaryReadiness=PrimaryFillLive|PrimaryStrokeLive|PrimaryPairLive;
    require(applyGate(state,false)==ElementGate::Ready,"Both elements gate");
    state.primaryReadiness=PrimaryFillLive;require(applyGate(state,false)==ElementGate::MissingStroke,"Missing stroke admitted");
    state.primaryReadiness=PrimaryStrokeLive;require(applyGate(state,false)==ElementGate::MissingFill,"Missing fill admitted");
    state.primaryReadiness=PrimaryFillLive|PrimaryStrokeLive|PrimaryPairLive;state.error=E_FAIL;require(applyGate(state,false)==ElementGate::CallbackFailure,"Callback error admitted");
    state.error=0;require(applyGate(state,true)==ElementGate::Cancelled,"Late startup applied after Quit");
    state.stopping=1;require(applyGate(state,false)==ElementGate::Cancelled,"Apply admitted after stop");
    printf("PASS missing_elements_callback_failure_and_startup_stop_rejected\n");
    require(classifyDll(L"TinyTaskbarTap-v2.dll")==DllGate::OlderOrOtherVersion && classifyDll(L"TINYTASKBARTAP-V3.DLL")==DllGate::Current && classifyDll(L"user32.dll")==DllGate::Unrelated,"Version reuse/refusal gate");
    printf("PASS legacy_version_refused_and_current_version_reused\n");
    printf("v3 protocol layout validated; offline checks only, no DLL load.\n");return 0;
}catch(std::exception const& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
