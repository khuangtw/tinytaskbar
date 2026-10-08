// SPDX-License-Identifier: GPL-3.0-only
// Build helper only: hash the matching DLL without changing script policies.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <array>
#include <vector>
#include <stdexcept>
static void check(bool ok,char const* reason){if(!ok)throw std::runtime_error(reason);}
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct Hash {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    std::vector<unsigned char> object;
    ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
};
int wmain(int argc,wchar_t** argv) try {
    check(argc==3,"Pass the DLL and output header paths.");
    File dll;dll.value=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(dll.value!=INVALID_HANDLE_VALUE,"Cannot open the built DLL.");LARGE_INTEGER size={};check(GetFileSizeEx(dll.value,&size)!=FALSE,"Cannot read DLL size.");
    Hash h;check(BCryptOpenAlgorithmProvider(&h.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Cannot open SHA-256.");
    ULONG objectBytes=0,resultBytes=0;check(BCryptGetProperty(h.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectBytes),sizeof(objectBytes),&resultBytes,0)>=0,"Cannot query hash workspace.");
    h.object.resize(objectBytes);check(BCryptCreateHash(h.algorithm,&h.hash,h.object.data(),objectBytes,nullptr,0,0)>=0,"Cannot create SHA-256.");
    std::array<unsigned char,32768> buffer={};DWORD count=0;
    for(;;){check(ReadFile(dll.value,buffer.data(),DWORD(buffer.size()),&count,nullptr)!=FALSE,"Cannot read DLL.");if(!count)break;check(BCryptHashData(h.hash,buffer.data(),count,0)>=0,"Cannot hash DLL.");}
    std::array<unsigned char,32> digest={};check(BCryptFinishHash(h.hash,digest.data(),ULONG(digest.size()),0)>=0,"Cannot finish SHA-256.");
    char hex[65]={};constexpr char digits[]="0123456789ABCDEF";
    for(size_t i=0;i<digest.size();++i){hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15];}
    FILE* output=nullptr;check(_wfopen_s(&output,argv[2],L"wb")==0 && output,"Cannot create build identity header.");
    int written=fprintf(output,"// Generated build identity; rebuild both binaries together.\n#pragma once\ninline constexpr wchar_t ExpectedDllHash[]=L\"%s\";\ninline constexpr LONGLONG ExpectedDllBytes=%lld;\n",hex,size.QuadPart);
    int closed=fclose(output);check(written>0 && closed==0,"Cannot finish build identity header.");return 0;
}catch(std::exception const& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
