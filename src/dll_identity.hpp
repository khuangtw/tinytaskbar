// GPL-3.0. Identity check shared by the manual controller and offline tests.
#pragma once
#include "protocol.hpp"
#include <bcrypt.h>
#include <array>
#include <vector>
#include <stdexcept>

// Generated after building the DLL; a fork's controller must match its own DLL.
#include "dll_build_identity.hpp"
struct OwnedHandle {
    HANDLE value=nullptr;
    OwnedHandle()=default;
    OwnedHandle(OwnedHandle const&)=delete;
    OwnedHandle& operator=(OwnedHandle const&)=delete;
    ~OwnedHandle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
inline void require(bool ok,char const* reason){if(!ok)throw std::runtime_error(reason);}
struct TrustedDllFile {
    OwnedHandle file;
    std::wstring path;
    explicit TrustedDllFile(std::wstring location):path(std::move(location)) {
        // Keep this read handle through the session; deny writes/deletion while
        // hashing and loading the reviewed DLL, instead of a hash/load gap.
        file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(file.value!=INVALID_HANDLE_VALUE,"Cannot read the reviewed DLL file.");
        LARGE_INTEGER bytes={};
        require(GetFileSizeEx(file.value,&bytes) && bytes.QuadPart==ExpectedDllBytes,"DLL size differs from the reviewed file.");
        struct Hash {
            BCRYPT_ALG_HANDLE algorithm=nullptr;
            BCRYPT_HASH_HANDLE hash=nullptr;
            std::vector<unsigned char> object;
            ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
        } state;
        require(BCryptOpenAlgorithmProvider(&state.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Cannot open SHA-256.");
        ULONG objectBytes=0,count=0;
        require(BCryptGetProperty(state.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectBytes),sizeof(objectBytes),&count,0)>=0,"Cannot query hash workspace.");
        state.object.resize(objectBytes);
        require(BCryptCreateHash(state.algorithm,&state.hash,state.object.data(),objectBytes,nullptr,0,0)>=0,"Cannot initialize SHA-256.");
        std::array<unsigned char,32768> buffer={};DWORD read=0;
        for(;;) {
            require(ReadFile(file.value,buffer.data(),DWORD(buffer.size()),&read,nullptr)!=FALSE,"Cannot read DLL for hashing.");
            if(!read)break;
            require(BCryptHashData(state.hash,buffer.data(),read,0)>=0,"Cannot hash DLL.");
        }
        std::array<unsigned char,32> digest={};
        require(BCryptFinishHash(state.hash,digest.data(),ULONG(digest.size()),0)>=0,"Cannot finish SHA-256.");
        std::wstring hex;constexpr wchar_t digits[]=L"0123456789ABCDEF";
        for(auto byte:digest){hex+=digits[byte>>4];hex+=digits[byte&15];}
        require(hex==ExpectedDllHash,"DLL SHA-256 differs from the reviewed source build.");
    }
};
