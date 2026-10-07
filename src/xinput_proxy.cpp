#include <windows.h>
#include <xinput.h>
#include <stdexcept>
#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include "gamepad_ipc.hpp"

static INIT_ONCE init=INIT_ONCE_STATIC_INIT;
static HMODULE systemInput=nullptr;
static HANDLE mapping=nullptr;
static SharedGamepad* shared=nullptr;
static volatile LONG sharedHasMotors=1;     // 0 when the bridge's mapping is an older build's (no room for rumble)
static BOOL CALLBACK initialize(PINIT_ONCE,PVOID,PVOID*) {
    wchar_t path[MAX_PATH]{};
    GetSystemDirectoryW(path,MAX_PATH);
    wcscat_s(path,L"\\xinput1_4.dll");
    systemInput=LoadLibraryW(path); // Absolute system path prevents recursive proxy loading.
    return TRUE;
}
template<class T> static T native(const char* name) {
    InitOnceExecuteOnce(&init,initialize,nullptr,nullptr);
    return systemInput ? reinterpret_cast<T>(GetProcAddress(systemInput,name)) : nullptr;
}
// A small log next to this DLL (flattodepth-shim.log), so "the game never asks for the controller" can be told apart from "the
// game asks and gets nothing". A few dozen short lines per run at most; nothing is logged if the folder cannot be written.
static volatile LONG logLines=0;
static void shimLog(const char* format,...) {
    if (InterlockedIncrement(&logLines)>150) return;
    static wchar_t path[MAX_PATH]{}; static INIT_ONCE pathOnce=INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&pathOnce,[](PINIT_ONCE,PVOID,PVOID*) -> BOOL {
        HMODULE self=nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&shimLog),&self) && GetModuleFileNameW(self,path,MAX_PATH)) {
            wchar_t* slash=wcsrchr(path,L'\\'); if (slash) { *(slash+1)=0; wcscat_s(path,MAX_PATH,L"flattodepth-shim.log"); } else path[0]=0;
        }
        return TRUE;
    },nullptr,nullptr);
    if (!path[0]) return;
    wchar_t self[MAX_PATH]{}; GetModuleFileNameW(nullptr,self,MAX_PATH);
    const wchar_t* exe=wcsrchr(self,L'\\'); exe=exe ? exe+1 : self;
    char line[400]; int n=_snprintf_s(line,sizeof(line),_TRUNCATE,"[%llu ms] pid=%lu %ls: ",GetTickCount64(),GetCurrentProcessId(),exe);
    va_list args; va_start(args,format); n+=_vsnprintf_s(line+n,sizeof(line)-static_cast<size_t>(n),_TRUNCATE,format,args); va_end(args);
    n=std::min<int>(n,static_cast<int>(sizeof(line))-3); line[n++]='\r'; line[n++]='\n';
    const HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return;
    DWORD written=0; WriteFile(file,line,static_cast<DWORD>(n),&written,nullptr); CloseHandle(file);
}
// Says why a reading was refused, for the log.
static const char* whyRefused(const SharedGamepad* s) {
    if (s->magic!=GamepadMagic || s->version!=1) return "the shared block is not a FlatToDepth gamepad block";
    if (!s->connected) return "the bridge says no controller is connected";
    if (GetTickCount64()-s->heartbeat>500) return "the bridge has stopped updating (heartbeat older than 500 ms)";
    return "torn read";
}
static volatile LONG calls[8],hits[8];       // per export: calls, and readings the game got from the VR controllers
static void noteCall(int which,const char* name,DWORD index) {
    if (InterlockedIncrement(&calls[which])==1) {
        wchar_t self[MAX_PATH]{}; HMODULE m=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&noteCall),&m); GetModuleFileNameW(m,self,MAX_PATH);
        const wchar_t* dll=wcsrchr(self,L'\\'); shimLog("first call of %s(%lu) (this shim was loaded as %ls)",name,index,dll ? dll+1 : self);
    }
}
static void summary() {
    static volatile LONGLONG next=0; const ULONGLONG now=GetTickCount64();
    LONGLONG due=InterlockedCompareExchange64(&next,0,0);
    if (due==0) { InterlockedCompareExchange64(&next,static_cast<LONGLONG>(now+5000),0); return; }
    if (static_cast<LONGLONG>(now)<due || InterlockedCompareExchange64(&next,static_cast<LONGLONG>(now+10000),due)!=due) return;
    shimLog("calls GetState=%ld(%ld read) SetState=%ld(%ld) Capabilities=%ld(%ld) Battery=%ld Keystroke=%ld",calls[0],hits[0],calls[1],hits[1],calls[2],hits[2],calls[3],calls[4]);
}
static bool virtualState(DWORD index,XINPUT_STATE& state,int which=0) {
    summary();
    if (index!=0) return false;
    // These functions are called from the game thread, never inside DllMain.
    if (!shared) {
        wchar_t testName[128]{};
        GetEnvironmentVariableW(L"FLATTODEPTH_CONTROLLER_TEST_MAPPING",testName,128);
        const bool testing=wcsncmp(testName,GamepadTestPrefix,wcslen(GamepadTestPrefix))==0;
        HANDLE opened=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,testing ? testName : GamepadMappingName);
        if (!opened) { static volatile LONG once; if (InterlockedExchange(&once,1)==0) shimLog("cannot open the bridge's mapping (error %lu): is FlatToDepth running, in the same Windows session?",GetLastError()); }
        if (opened) {
            auto view=static_cast<SharedGamepad*>(MapViewOfFile(opened,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedGamepad)));
            bool motors=true;
            if (!view) { view=static_cast<SharedGamepad*>(MapViewOfFile(opened,FILE_MAP_ALL_ACCESS,0,0,LegacyGamepadSize)); motors=false; }
            if (view) {
                // Say whether this mapping has room for rumble before anyone can see the pointer: another game
                // thread may be right behind this one. Both threads would write the same answer for the same mapping.
                InterlockedExchange(&sharedHasMotors,motors ? 1 : 0);
                if (InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&shared),view,nullptr)!=nullptr) {
                    UnmapViewOfFile(view); CloseHandle(opened);
                } else { mapping=opened; shimLog("opened the bridge's mapping (rumble %s)",motors ? "supported" : "not supported by that bridge"); }
            } else CloseHandle(opened);
        }
    }
    if (!shared) return false;
    if (!readGamepad(shared,state)) {
        static volatile LONG refused;
        const LONG n=InterlockedIncrement(&refused);
        if (n<=6 || n%1000==0) shimLog("reading refused (%ld so far): %s",n,whyRefused(shared));
        return false;
    }
    InterlockedIncrement(&hits[which]);
    static volatile LONG firstHit; if (InterlockedExchange(&firstHit,1)==0) shimLog("first reading handed to the game: buttons=0x%04x",state.Gamepad.wButtons);
    InterlockedIncrement(&shared->readCount);
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&shared->readerPid),static_cast<LONG>(GetCurrentProcessId()));
    return true;
}
extern "C" DWORD WINAPI FtdGetState(DWORD index,XINPUT_STATE* state) {
    noteCall(0,"XInputGetState",index);
    if (!state || index>3) return ERROR_BAD_ARGUMENTS;
    if (virtualState(index,*state,0)) return ERROR_SUCCESS;
    auto fn=native<decltype(&XInputGetState)>("XInputGetState");
    return fn ? fn(index,state) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI FtdSetState(DWORD index,XINPUT_VIBRATION* vibration) {
    noteCall(1,"XInputSetState",index);
    if (!vibration || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{};
    if (virtualState(index,state,1)) {
        // The virtual pad has no motors; hand the request to the bridge, which plays it on the VR controllers.
        if (InterlockedCompareExchange(&sharedHasMotors,1,1)==1) {
            InterlockedExchange(&shared->motors,packMotors(vibration->wLeftMotorSpeed,vibration->wRightMotorSpeed));
            InterlockedIncrement(&shared->motorWrites);
        }
        return ERROR_SUCCESS;
    }
    auto fn=native<decltype(&XInputSetState)>("XInputSetState");
    return fn ? fn(index,vibration) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI FtdGetCapabilities(DWORD index,DWORD flags,XINPUT_CAPABILITIES* caps) {
    noteCall(2,"XInputGetCapabilities",index);
    if (!caps || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{};
    if (virtualState(index,state,2)) {
        *caps={}; caps->Type=XINPUT_DEVTYPE_GAMEPAD; caps->SubType=XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Gamepad.wButtons=0xf3ff; caps->Gamepad.bLeftTrigger=255; caps->Gamepad.bRightTrigger=255;
        caps->Gamepad.sThumbLX=caps->Gamepad.sThumbLY=caps->Gamepad.sThumbRX=caps->Gamepad.sThumbRY=32767;
        return ERROR_SUCCESS;
    }
    auto fn=native<decltype(&XInputGetCapabilities)>("XInputGetCapabilities");
    return fn ? fn(index,flags,caps) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" void WINAPI FtdEnable(BOOL enabled) {
    noteCall(6,"XInputEnable",enabled);
    auto fn=native<void (WINAPI*)(BOOL)>("XInputEnable"); if (fn) fn(enabled);
}
extern "C" DWORD WINAPI FtdGetBattery(DWORD index,BYTE type,XINPUT_BATTERY_INFORMATION* info) {
    noteCall(3,"XInputGetBatteryInformation",index);
    if (!info || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{};
    if (virtualState(index,state,3)) { *info={BATTERY_TYPE_WIRED,BATTERY_LEVEL_FULL}; return ERROR_SUCCESS; }
    auto fn=native<decltype(&XInputGetBatteryInformation)>("XInputGetBatteryInformation");
    return fn ? fn(index,type,info) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI FtdGetKeystroke(DWORD index,DWORD reserved,XINPUT_KEYSTROKE* key) {
    noteCall(4,"XInputGetKeystroke",index);
    if (!key || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{}; if (virtualState(index,state,4)) { *key={}; return ERROR_EMPTY; }
    auto fn=native<decltype(&XInputGetKeystroke)>("XInputGetKeystroke");
    return fn ? fn(index,reserved,key) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI FtdGetAudio(DWORD index,WCHAR* render,UINT* renderSize,WCHAR* capture,UINT* captureSize) {
    auto fn=native<decltype(&XInputGetAudioDeviceIds)>("XInputGetAudioDeviceIds");
    return fn ? fn(index,render,renderSize,capture,captureSize) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI FtdGetDSound(DWORD index,GUID* render,GUID* capture) {
    if (!render || !capture || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{}; if (virtualState(index,state,5)) { *render={}; *capture={}; return ERROR_SUCCESS; }
    return ERROR_DEVICE_NOT_CONNECTED;
}
