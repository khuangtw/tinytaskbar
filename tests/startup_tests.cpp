// SPDX-License-Identifier: GPL-3.0-only
// Isolated shortcut fixtures only; no real Startup folder or Explorer access.
#include "startup.hpp"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

static void expect(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
static std::vector<char> bytes(std::wstring const& path) {
    std::ifstream input(path,std::ios::binary);expect(bool(input),"Cannot read fixture.");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
struct Fixtures {
    std::wstring root,folder,first,second;
    std::vector<std::wstring> files,folders;
    Fixtures() {
        wchar_t current[32768]={};expect(GetCurrentDirectoryW(32768,current)>0,"Cannot get isolated test directory.");
        GUID id={};expect(SUCCEEDED(CoCreateGuid(&id)),"Cannot name fixture directory.");
        wchar_t suffix[40]={};expect(StringFromGUID2(id,suffix,40)>0,"Cannot format fixture name.");
        root=std::wstring(current)+L"\\startup-fixtures-"+suffix;
        // Exact newly-created paths only. No recursive delete or folder scan.
        directory(root);folder=root+L"\\Startup fixture";directory(folder);
        auto a=root+L"\\First copy with spaces",b=root+L"\\Moved copy with spaces";directory(a);directory(b);
        first=a+L"\\TinyTaskbar.exe";second=b+L"\\TinyTaskbar.exe";touch(first);touch(second);
        files.push_back(folder+L"\\TinyTaskbar.lnk");
    }
    void directory(std::wstring const& path){expect(CreateDirectoryW(path.c_str(),nullptr)!=FALSE,"Cannot create unique fixture directory.");folders.push_back(path);}
    void touch(std::wstring const& path) {
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        expect(file!=INVALID_HANDLE_VALUE,"Cannot create fixture target.");CloseHandle(file);files.push_back(path);
    }
    ~Fixtures(){for(auto const& path:files)DeleteFileW(path.c_str());for(auto it=folders.rbegin();it!=folders.rend();++it)RemoveDirectoryW(it->c_str());}
};
static void verifyShortcut(std::wstring const& path,std::wstring const& target) {
    expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Cannot initialize fixture COM.");
    struct Uninit {~Uninit(){CoUninitialize();}} apartment;
    IShellLinkW* link=nullptr;expect(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link))),"Cannot open fixture link.");
    struct ReleaseLink {IShellLinkW* value;~ReleaseLink(){value->Release();}} releaseLink{link};
    IPersistFile* file=nullptr;expect(SUCCEEDED(link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&file))),"Cannot read fixture link.");
    struct ReleaseFile {IPersistFile* value;~ReleaseFile(){value->Release();}} releaseFile{file};
    expect(SUCCEEDED(file->Load(path.c_str(),STGM_READ)),"Cannot load fixture link.");
    std::vector<wchar_t> actual(32768),arguments(32768),working(32768);
    expect(SUCCEEDED(link->GetPath(actual.data(),int(actual.size()),nullptr,SLGP_RAWPATH)) && target==actual.data(),"Spaced target path was changed/quoted incorrectly.");
    expect(SUCCEEDED(link->GetArguments(arguments.data(),int(arguments.size()))) && arguments[0]==0,"Startup supplied unexpected arguments.");
    expect(SUCCEEDED(link->GetWorkingDirectory(working.data(),int(working.size()))) && target.substr(0,target.find_last_of(L'\\'))==working.data(),"Wrong shortcut working directory.");
}
static void foreignShortcut(std::wstring const& path,std::wstring const& target) {
    expect(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Cannot initialize fixture COM.");
    IShellLinkW* link=nullptr;IPersistFile* file=nullptr;
    struct Cleanup {IShellLinkW*& link;IPersistFile*& file;~Cleanup(){if(file)file->Release();if(link)link->Release();CoUninitialize();}} cleanup{link,file};
    expect(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link))),"Cannot create foreign fixture.");
    expect(SUCCEEDED(link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&file))) && SUCCEEDED(link->SetPath(target.c_str())) && SUCCEEDED(link->SetDescription(L"User-created unrelated shortcut")) && SUCCEEDED(file->Save(path.c_str(),TRUE)),"Cannot save foreign fixture.");
}
template<class F> static void rejected(F action){bool failed=false;try{action();}catch(std::runtime_error const&){failed=true;}expect(failed,"An unmanaged entry was changed.");}
#ifdef TINY_TASKBAR_STARTUP_TESTS
static std::wstring raceForeign;
static bool replaced=false,written=false;
static DWORD replacementError=0,writeError=0;
static bool sharingDenied(DWORD error){return error==ERROR_SHARING_VIOLATION || error==ERROR_ACCESS_DENIED;}
static void replaceRace(wchar_t const* target) {
    replaced=MoveFileExW(raceForeign.c_str(),target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;replacementError=GetLastError();
    HANDLE attempt=CreateFileW(target,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    written=attempt!=INVALID_HANDLE_VALUE;writeError=GetLastError();if(written)CloseHandle(attempt);
}
static void absentRace(wchar_t const* target) {
    HANDLE file=CreateFileW(target,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);expect(file!=INVALID_HANDLE_VALUE,"Cannot inject absent-entry race.");
    constexpr char text[]="External unmanaged replacement";DWORD count=0;BOOL ok=WriteFile(file,text,DWORD(sizeof(text)-1),&count,nullptr);CloseHandle(file);expect(ok && count==sizeof(text)-1,"Cannot write race fixture.");
}
static void raceTests() {
    Fixtures f;auto path=f.folder+L"\\TinyTaskbar.lnk";raceForeign=f.root+L"\\External unmanaged.lnk";f.files.push_back(raceForeign);
    {std::ofstream output(raceForeign,std::ios::binary);output<<"Foreign content must remain unchanged.";expect(bool(output),"Cannot prepare race fixture.");}
    auto foreign=bytes(raceForeign);startup::enable(f.folder,f.first);
    struct ResetHook {~ResetHook(){startup::test::setMutationHook(nullptr);}} reset;
    startup::test::setMutationHook(replaceRace);startup::enable(f.folder,f.second);
    printf("enable_race replaced=%d replacement_error=%lu written=%d write_error=%lu foreign_preserved=%d\n",int(replaced),replacementError,int(written),writeError,int(bytes(raceForeign)==foreign));
    expect(!replaced && !written && sharingDenied(replacementError) && sharingDenied(writeError) && bytes(raceForeign)==foreign,"Concurrent replacement/write changed an entry after enable ownership check.");
    startup::test::setMutationHook(nullptr);verifyShortcut(path,f.second);
    printf("PASS production_enable_locked_identity_blocks_replacement_and_write\n");
    startup::test::setMutationHook(replaceRace);startup::disable(f.folder);
    printf("disable_race replaced=%d replacement_error=%lu written=%d write_error=%lu foreign_preserved=%d\n",int(replaced),replacementError,int(written),writeError,int(bytes(raceForeign)==foreign));
    expect(!replaced && !written && sharingDenied(replacementError) && sharingDenied(writeError) && bytes(raceForeign)==foreign && GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES,"Disable deleted an unchecked replacement.");
    printf("PASS production_disable_deletes_only_checked_locked_handle\n");
    startup::test::setMutationHook(absentRace);rejected([&]{startup::enable(f.folder,f.first);});startup::test::setMutationHook(nullptr);
    expect(startup::inspect(f.folder,f.first)==startup::Status::Foreign,"Create-new race overwrote the appearing foreign entry.");
    auto appearing=bytes(path);expect(std::string(appearing.begin(),appearing.end())=="External unmanaged replacement","Appearing foreign bytes were changed.");
    printf("PASS production_create_new_preserves_entry_appearing_after_absence\n");
}
#endif
int wmain() try {
    Fixtures f;auto path=f.folder+L"\\TinyTaskbar.lnk";
    expect(startup::parse(L"--stop")==startup::Action::None && startup::parse(L"--startup-enable")==startup::Action::Enable && startup::parse(L"--startup-disable")==startup::Action::Disable && startup::parse(L"--startup-remove")==startup::Action::Disable && startup::parse(L"--startup-status")==startup::Action::Status,"CLI action parsing.");
    expect(startup::inspect(f.folder,f.first)==startup::Status::Off,"Default startup was enabled.");
    printf("PASS explicit_actions_and_default_off\n");
    startup::enable(f.folder,f.first);verifyShortcut(path,f.first);
    expect(startup::inspect(f.folder,f.first)==startup::Status::Present,"Enable did not register this target.");
    printf("PASS_absolute_spaced_target_and_working_directory\n");
    auto original=bytes(path);startup::enable(f.folder,f.first);expect(original==bytes(path),"Repeated enable rewrote the shortcut.");
    printf("PASS idempotent_enable\n");
    expect(startup::inspect(f.folder,f.second)==startup::Status::OtherLocation,"Another copy was not detected.");
    startup::enable(f.folder,f.second);verifyShortcut(path,f.second);
    expect(startup::inspect(f.folder,f.second)==startup::Status::Present,"Moved-copy enable did not update target.");
    printf("PASS moved_copy_updates_managed_target\n");
    expect(DeleteFileW(f.second.c_str())!=FALSE,"Cannot simulate missing target.");
    expect(startup::inspect(f.folder,f.first)==startup::Status::MissingTarget,"Missing target was not reported.");
    startup::enable(f.folder,f.first);expect(startup::inspect(f.folder,f.first)==startup::Status::Present,"Stale target could not be repaired.");
    expect(DeleteFileW(f.first.c_str())!=FALSE,"Cannot simulate stale uninstall.");
    startup::disable(f.folder);startup::disable(f.folder);expect(startup::inspect(f.folder,f.first)==startup::Status::Off,"Stale uninstall did not remove the entry.");
    printf("PASS stale_repair_and_idempotent_uninstall_cleanup\n");
    f.touch(f.first);
    {std::ofstream output(path,std::ios::binary);output<<"This unrelated file must survive.";expect(bool(output),"Cannot create foreign file.");}
    auto foreign=bytes(path);expect(startup::inspect(f.folder,f.first)==startup::Status::Foreign,"Invalid unmanaged entry not detected.");
    rejected([&]{startup::enable(f.folder,f.first);});rejected([&]{startup::disable(f.folder);});expect(foreign==bytes(path),"Foreign file contents changed.");
    printf("PASS unmanaged_file_preserved\n");
    expect(DeleteFileW(path.c_str())!=FALSE,"Cannot clear our foreign fixture.");foreignShortcut(path,f.first);foreign=bytes(path);
    rejected([&]{startup::enable(f.folder,f.first);});rejected([&]{startup::disable(f.folder);});expect(foreign==bytes(path),"User-created shortcut changed.");
    printf("PASS unmanaged_shortcut_preserved\n");
#ifdef TINY_TASKBAR_STARTUP_TESTS
    raceTests();printf("10 startup groups passed; no Explorer or real Startup access.\n");
#else
    printf("7 startup groups passed; no Explorer or real Startup access.\n");
#endif
    return 0;
}catch(std::exception const& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
