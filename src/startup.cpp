// SPDX-License-Identifier: GPL-3.0-only
#include "startup.hpp"
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <stdexcept>
#include <vector>

namespace startup {
#ifdef TINY_TASKBAR_STARTUP_TESTS
namespace test {
static void (*mutationHook)(wchar_t const*)=nullptr;
void setMutationHook(void (*hook)(wchar_t const*)) noexcept {mutationHook=hook;}
}
#endif
namespace {
constexpr wchar_t LinkName[]=L"TinyTaskbar.lnk";
constexpr wchar_t Marker[]=L"TinyTaskbar login startup; managed by TinyTaskbar v1";
constexpr LONGLONG MaxLinkBytes=1024*1024;
void check(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
void checkHr(HRESULT result,char const* message){check(SUCCEEDED(result),message);}
template<class T> struct Com {
    T* value=nullptr;
    Com()=default;Com(Com const&)=delete;Com& operator=(Com const&)=delete;
    ~Com(){if(value)value->Release();}
    T* operator->() const{return value;}
};
struct Apartment {
    Apartment(){checkHr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE),"Cannot initialize the shortcut API.");}
    ~Apartment(){CoUninitialize();}
};
struct Lock {
    HANDLE value=nullptr;bool acquired=false;
    Lock() {
        value=CreateMutexW(nullptr,FALSE,L"Local\\TinyTaskbar.Startup.2cb3b83d.v1");check(value!=nullptr,"Cannot open the startup command lock.");
        DWORD result=WaitForSingleObject(value,5000);
        if(result!=WAIT_OBJECT_0 && result!=WAIT_ABANDONED){CloseHandle(value);value=nullptr;check(false,"Another startup command has not finished.");}
        acquired=true;
    }
    ~Lock(){if(acquired)ReleaseMutex(value);if(value)CloseHandle(value);}
};
std::wstring linkPath(std::wstring const& folder){check(!folder.empty(),"Startup folder is unavailable.");return folder+L"\\"+LinkName;}
bool regular(std::wstring const& path) {
    DWORD attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
struct EntryFile {
    HANDLE value=INVALID_HANDLE_VALUE;
    EntryFile(std::wstring const& path,DWORD access,DWORD creation=OPEN_EXISTING) {
        // Deny other writers AND rename/delete for the entire ownership check
        // and mutation. Operations below use this same handle, never reopen path.
        value=CreateFileW(path.c_str(),access,FILE_SHARE_READ,nullptr,creation,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(value==INVALID_HANDLE_VALUE) {
            DWORD error=GetLastError();
            if(creation==OPEN_EXISTING && (error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND))return;
            check(false,"Cannot lock the startup entry; no existing file was changed.");
        }
    }
    EntryFile(EntryFile const&)=delete;EntryFile& operator=(EntryFile const&)=delete;
    ~EntryFile(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    bool present() const{return value!=INVALID_HANDLE_VALUE;}
    bool regular() const {
        BY_HANDLE_FILE_INFORMATION info={};check(GetFileInformationByHandle(value,&info)!=FALSE,"Cannot inspect the locked startup entry.");
        return !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    }
    std::vector<unsigned char> contents() const {
        LARGE_INTEGER size={};check(GetFileSizeEx(value,&size)!=FALSE,"Cannot read startup entry size.");
        if(size.QuadPart<=0 || size.QuadPart>MaxLinkBytes)return {};
        std::vector<unsigned char> bytes(size_t(size.QuadPart));DWORD count=0;
        check(ReadFile(value,bytes.data(),DWORD(bytes.size()),&count,nullptr) && count==bytes.size(),"Cannot read the locked startup entry.");return bytes;
    }
    bool write(std::vector<unsigned char> const& bytes) const noexcept {
        LARGE_INTEGER zero={};if(!SetFilePointerEx(value,zero,nullptr,FILE_BEGIN))return false;
        size_t position=0;
        while(position<bytes.size()) {DWORD count=0;if(!WriteFile(value,bytes.data()+position,DWORD(bytes.size()-position),&count,nullptr) || !count)return false;position+=count;}
        return SetEndOfFile(value) && FlushFileBuffers(value);
    }
    bool remove() const noexcept {
        FILE_DISPOSITION_INFO disposition={TRUE};
        return SetFileInformationByHandle(value,FileDispositionInfo,&disposition,sizeof(disposition))!=FALSE;
    }
};
void createLink(Com<IShellLinkW>& link,Com<IPersistStream>& persistence) {
    checkHr(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link.value)),"Cannot create a Windows shortcut.");
    checkHr(link->QueryInterface(IID_IPersistStream,reinterpret_cast<void**>(&persistence.value)),"Cannot serialize a Windows shortcut.");
}
void memoryStream(Com<IStream>& stream,std::vector<unsigned char> const& bytes) {
    checkHr(CreateStreamOnHGlobal(nullptr,TRUE,&stream.value),"Cannot allocate shortcut stream.");
    if(!bytes.empty()){ULONG written=0;checkHr(stream->Write(bytes.data(),ULONG(bytes.size()),&written),"Cannot write shortcut stream.");check(written==bytes.size(),"Truncated shortcut stream.");}
    LARGE_INTEGER zero={};checkHr(stream->Seek(zero,STREAM_SEEK_SET,nullptr),"Cannot seek shortcut stream.");
}
struct Entry {bool present=false,owned=false;std::wstring target;std::vector<unsigned char> original;};
Entry read(EntryFile const& file) {
    if(!file.present())return {};
    Entry result;result.present=true;if(!file.regular())return result;
    result.original=file.contents();if(result.original.empty())return result;
    Com<IShellLinkW> link;Com<IPersistStream> persistence;createLink(link,persistence);
    Com<IStream> stream;memoryStream(stream,result.original);
    if(FAILED(persistence->Load(stream.value)))return result;
    std::vector<wchar_t> target(32768),arguments(32768),description(1024);
    if(FAILED(link->GetPath(target.data(),int(target.size()),nullptr,SLGP_RAWPATH)) ||
       FAILED(link->GetArguments(arguments.data(),int(arguments.size()))) ||
       FAILED(link->GetDescription(description.data(),int(description.size()))))return result;
    // Load from checked-handle bytes; never Resolve or reopen a mutable pathname.
    result.target=target.data();result.owned=!result.target.empty() && arguments[0]==0 && wcscmp(description.data(),Marker)==0;return result;
}
Status classify(Entry const& entry,std::wstring const& executable) {
    if(!entry.present)return Status::Off;if(!entry.owned)return Status::Foreign;
    if(!regular(entry.target))return Status::MissingTarget;
    return _wcsicmp(entry.target.c_str(),executable.c_str())==0?Status::Present:Status::OtherLocation;
}
std::vector<unsigned char> serialize(std::wstring const& executable) {
    Com<IShellLinkW> link;Com<IPersistStream> persistence;createLink(link,persistence);
    checkHr(link->SetPath(executable.c_str()),"Cannot set the startup executable.");checkHr(link->SetArguments(L""),"Cannot set startup arguments.");
    auto separator=executable.find_last_of(L"\\/");check(separator!=std::wstring::npos,"The executable path must be absolute.");
    checkHr(link->SetWorkingDirectory(executable.substr(0,separator).c_str()),"Cannot set startup working directory.");
    checkHr(link->SetDescription(Marker),"Cannot mark the managed shortcut.");checkHr(link->SetShowCmd(SW_SHOWNORMAL),"Cannot set startup display behavior.");
    Com<IStream> stream;memoryStream(stream,{});checkHr(persistence->Save(stream.value,FALSE),"Cannot serialize startup shortcut.");
    STATSTG info={};checkHr(stream->Stat(&info,STATFLAG_NONAME),"Cannot get serialized shortcut size.");
    check(info.cbSize.QuadPart>0 && info.cbSize.QuadPart<=ULONGLONG(MaxLinkBytes),"Invalid serialized shortcut size.");
    LARGE_INTEGER zero={};checkHr(stream->Seek(zero,STREAM_SEEK_SET,nullptr),"Cannot seek serialized shortcut.");
    std::vector<unsigned char> bytes(size_t(info.cbSize.QuadPart));ULONG count=0;
    checkHr(stream->Read(bytes.data(),ULONG(bytes.size()),&count),"Cannot read serialized shortcut.");check(count==bytes.size(),"Truncated serialized shortcut.");return bytes;
}
void beforeMutation(std::wstring const& path) {
#ifdef TINY_TASKBAR_STARTUP_TESTS
    if(test::mutationHook)test::mutationHook(path.c_str());
#else
    (void)path;
#endif
}
}
Action parse(wchar_t const* option) noexcept {
    if(wcscmp(option,L"--startup-enable")==0)return Action::Enable;
    if(wcscmp(option,L"--startup-disable")==0 || wcscmp(option,L"--startup-remove")==0)return Action::Disable;
    if(wcscmp(option,L"--startup-status")==0)return Action::Status;return Action::None;
}
std::wstring userFolder() {
    PWSTR folder=nullptr;checkHr(SHGetKnownFolderPath(FOLDERID_Startup,0,nullptr,&folder),"Cannot find this user's Startup folder.");
    struct Free {PWSTR value;~Free(){CoTaskMemFree(value);}} cleanup{folder};return folder;
}
Status inspect(std::wstring const& folder,std::wstring const& executable) {
    Lock lock;Apartment apartment;EntryFile file(linkPath(folder),GENERIC_READ);return classify(read(file),executable);
}
void enable(std::wstring const& folder,std::wstring const& executable) {
    Lock lock;Apartment apartment;auto path=linkPath(folder);EntryFile existingFile(path,GENERIC_READ|GENERIC_WRITE|DELETE);auto existing=read(existingFile);
    check(!existing.present || existing.owned,"TinyTaskbar.lnk already exists and is not our managed entry; it was preserved.");
    check(regular(executable),"Keep TinyTaskbar.exe in a permanent local folder before enabling startup.");
    if(classify(existing,executable)==Status::Present)return;
    auto bytes=serialize(executable);beforeMutation(path);
    if(existing.present) {
        if(!existingFile.write(bytes)) {
            check(existingFile.write(existing.original),"Startup update failed and original bytes could not be restored; preserve the log.");
            check(false,"Startup update failed; original shortcut bytes were restored.");
        }
    } else {
        // Never replace a file that appeared after the absent-entry observation.
        EntryFile created(path,GENERIC_READ|GENERIC_WRITE|DELETE,CREATE_NEW);
        if(!created.write(bytes)){check(created.remove(),"Startup write failed and newly created entry could not be removed.");check(false,"Startup write failed; newly created entry was removed.");}
    }
}
void disable(std::wstring const& folder) {
    Lock lock;Apartment apartment;auto path=linkPath(folder);EntryFile file(path,GENERIC_READ|DELETE);auto entry=read(file);
    if(!entry.present)return;check(entry.owned,"TinyTaskbar.lnk is not our managed entry; it was preserved.");
    beforeMutation(path);check(file.remove(),"Cannot remove the checked managed startup entry.");
}
std::string describe(Status status) {
    switch(status) {
    case Status::Off:return "Startup shortcut: off.";
    case Status::Present:return "Startup shortcut: present for this executable. Windows may separately disable login startup.";
    case Status::MissingTarget:return "Startup shortcut: stale; its target is missing. Re-enable from the new folder, or disable/remove it.";
    case Status::OtherLocation:return "Startup shortcut: points to another copy. Re-enable to use this copy, or disable/remove it.";
    case Status::Foreign:return "Startup shortcut: name occupied by an unmanaged entry; it will be preserved.";
    }
    return "Startup shortcut: unknown.";
}
}
