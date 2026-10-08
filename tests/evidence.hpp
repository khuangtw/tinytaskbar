// GPL-3.0. Native taskbar-only evidence helpers; no accent or opacity calls.
#pragma once
#include "protocol.hpp"
#include <psapi.h>
#include <cstdio>
#include <vector>
#include <stdexcept>

struct Metrics {ULONGLONG cpu,working,privateBytes;};
inline Metrics measure(HANDLE p) {
    FILETIME c,e,k,u;PROCESS_MEMORY_COUNTERS_EX m={};m.cb=sizeof(m);
    if(!GetProcessTimes(p,&c,&e,&k,&u) || !GetProcessMemoryInfo(p,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)))throw std::runtime_error("resource query failed");
    return {fileTicks(k)+fileTicks(u),m.WorkingSetSize,m.PrivateUsage};
}
inline void printMetrics(char const* who,char const* when,Metrics m){
    printf("resource %s %s cpu_100ns=%llu working_set_bytes=%llu private_bytes=%llu\n",who,when,m.cpu,m.working,m.privateBytes);
}
inline std::vector<unsigned char> screenshot(RECT r,wchar_t const* path) {
    int width=r.right-r.left,height=r.bottom-r.top;
    if(width<=0 || height<=0 || width>16384 || height>512)throw std::runtime_error("invalid taskbar screenshot bounds");
    HDC screen=GetDC(nullptr),mem=screen?CreateCompatibleDC(screen):nullptr;BITMAPINFO i={};
    i.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);i.bmiHeader.biWidth=width;i.bmiHeader.biHeight=-height;
    i.bmiHeader.biPlanes=1;i.bmiHeader.biBitCount=32;i.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP bitmap=mem?CreateDIBSection(mem,&i,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;
    if(!bitmap){if(mem)DeleteDC(mem);if(screen)ReleaseDC(nullptr,screen);throw std::runtime_error("capture allocation failed");}
    HGDIOBJ old=SelectObject(mem,bitmap);BOOL ok=BitBlt(mem,0,0,width,height,screen,r.left,r.top,SRCCOPY|CAPTUREBLT);GdiFlush();
    std::vector<unsigned char> data;if(ok)data.assign(static_cast<unsigned char*>(pixels),static_cast<unsigned char*>(pixels)+size_t(width)*height*4);
    SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(nullptr,screen);if(!ok)throw std::runtime_error("capture failed");
    BITMAPFILEHEADER f={};f.bfType=0x4d42;f.bfOffBits=sizeof(f)+sizeof(i.bmiHeader);f.bfSize=f.bfOffBits+DWORD(data.size());
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot save evidence");DWORD count=0;
    ok=WriteFile(file,&f,sizeof(f),&count,nullptr) && WriteFile(file,&i.bmiHeader,sizeof(i.bmiHeader),&count,nullptr) && WriteFile(file,data.data(),DWORD(data.size()),&count,nullptr);
    CloseHandle(file);if(!ok)throw std::runtime_error("evidence write failed");return data;
}
inline void compare(std::vector<unsigned char> const& a,std::vector<unsigned char> const& b,int width,char const* title){
    unsigned changed=0,samples=0;ULONGLONG difference=0;
    if(a.size()!=b.size())throw std::runtime_error("capture size changed");
    for(int y=40;y<96;++y)for(int x=480;x<800 && x<width;++x){size_t p=size_t(y*width+x)*4;bool unequal=false;
        for(int c=0;c<3;++c){int d=int(a[p+c])-int(b[p+c]);difference+=unsigned(d<0?-d:d);unequal|=d!=0;}++samples;changed+=unequal;}
    printf("visual %s sampled_background_pixels=%u changed=%u mean_channel_difference=%.6f\n",title,samples,changed,samples?double(difference)/(samples*3):0);
}
inline void sharpStripes(std::vector<unsigned char> const& image,int width){
    unsigned same=0,n=0;for(int y=40;y<96;++y)for(int x=480;x<800 && x<width;++x){size_t a=size_t(16*width+x)*4,b=size_t(y*width+x)*4;bool equal=true;
        for(int c=0;c<3;++c)equal&=image[a+c]==image[b+c];same+=equal;++n;}
    printf("visual clear_background_match=%u/%u (compared with the same column in unobscured stripes)\n",same,n);
}
inline unsigned topEdgeMatch(std::vector<unsigned char> const& image,int width,char const* when) {
    unsigned same=0,total=0;
    for(int y=32;y<40;++y)for(int x=0;x<width;++x){
        size_t a=size_t(16*width+x)*4,b=size_t(y*width+x)*4;bool equal=true;
        for(int c=0;c<3;++c)equal&=image[a+c]==image[b+c];same+=equal;++total;
    }
    printf("visual top_edge_%s_match=%u/%u (all columns, first eight physical taskbar rows)\n",when,same,total);return same;
}
inline unsigned topEdgeRestore(std::vector<unsigned char> const& before,std::vector<unsigned char> const& after,int width) {
    unsigned changed=0,total=0;
    for(int y=32;y<40;++y)for(int x=0;x<width;++x){size_t p=size_t(y*width+x)*4;bool unequal=false;
        for(int c=0;c<3;++c)unequal|=before[p+c]!=after[p+c];changed+=unequal;++total;}
    printf("visual restored_top_edge_changed=%u/%u\n",changed,total);return changed;
}
inline LRESULT CALLBACK patternProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_PAINT){PAINTSTRUCT p;HDC dc=BeginPaint(w,&p);RECT r;GetClientRect(w,&r);COLORREF colors[]={RGB(220,40,40),RGB(40,220,40),RGB(40,40,220)};
        for(int x=0;x<r.right;x+=128){RECT stripe={x,0,x+128,r.bottom};HBRUSH b=CreateSolidBrush(colors[(x/128)%3]);FillRect(dc,&stripe,b);DeleteObject(b);}EndPaint(w,&p);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
inline bool pause(DWORD ms,UINT recreated){
    ULONGLONG end=GetTickCount64()+ms;for(;;){ULONGLONG now=GetTickCount64();if(now>=end)return true;
        if(MsgWaitForMultipleObjectsEx(0,nullptr,DWORD(end-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED)return false;
        MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT || m.message==recreated)return false;TranslateMessage(&m);DispatchMessageW(&m);}}
}
