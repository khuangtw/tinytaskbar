// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "win32_helpers.hpp"
inline std::vector<unsigned char> processUser(HANDLE process){
    OwnedHandle token;require(OpenProcessToken(process,TOKEN_QUERY,&token.value)!=FALSE,"Cannot read process owner.");
    DWORD bytes=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes);
    require(bytes>=sizeof(TOKEN_USER),"Cannot size process owner.");std::vector<unsigned char> result(bytes);
    require(GetTokenInformation(token.value,TokenUser,result.data(),bytes,&bytes)!=FALSE,"Cannot read process SID.");return result;
}
inline void sameUserSession(HANDLE process){
    DWORD caller=0,target=0;require(ProcessIdToSessionId(GetCurrentProcessId(),&caller) && ProcessIdToSessionId(GetProcessId(process),&target) && caller==target,"Process is in another session.");
    auto own=processUser(GetCurrentProcess()),other=processUser(process);
    require(EqualSid(reinterpret_cast<TOKEN_USER*>(own.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(other.data())->User.Sid)!=FALSE,"Process belongs to another user.");
}
inline ULONGLONG processBirth(HANDLE process){FILETIME c,e,k,u;require(GetProcessTimes(process,&c,&e,&k,&u)!=FALSE,"Cannot read process birth.");return fileTicks(c);}
