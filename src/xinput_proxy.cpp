#include <windows.h>
#include <xinput.h>
#include <stdexcept>
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
static bool virtualState(DWORD index,XINPUT_STATE& state) {
    if (index!=0) return false;
    // These functions are called from the game thread, never inside DllMain.
    if (!shared) {
        wchar_t testName[128]{};
        GetEnvironmentVariableW(L"FLATTODEPTH_CONTROLLER_TEST_MAPPING",testName,128);
        const bool testing=wcsncmp(testName,GamepadTestPrefix,wcslen(GamepadTestPrefix))==0;
        HANDLE opened=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,testing ? testName : GamepadMappingName);
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
                } else mapping=opened;
            } else CloseHandle(opened);
        }
    }
    if (!shared || !readGamepad(shared,state)) return false;
    InterlockedIncrement(&shared->readCount);
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&shared->readerPid),static_cast<LONG>(GetCurrentProcessId()));
    return true;
}
extern "C" DWORD WINAPI FtdGetState(DWORD index,XINPUT_STATE* state) {
    if (!state || index>3) return ERROR_BAD_ARGUMENTS;
    if (virtualState(index,*state)) return ERROR_SUCCESS;
    auto fn=native<decltype(&XInputGetState)>("XInputGetState");
    return fn ? fn(index,state) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI FtdSetState(DWORD index,XINPUT_VIBRATION* vibration) {
    if (!vibration || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{};
    if (virtualState(index,state)) {
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
    if (!caps || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{};
    if (virtualState(index,state)) {
        *caps={}; caps->Type=XINPUT_DEVTYPE_GAMEPAD; caps->SubType=XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Gamepad.wButtons=0xf3ff; caps->Gamepad.bLeftTrigger=255; caps->Gamepad.bRightTrigger=255;
        caps->Gamepad.sThumbLX=caps->Gamepad.sThumbLY=caps->Gamepad.sThumbRX=caps->Gamepad.sThumbRY=32767;
        return ERROR_SUCCESS;
    }
    auto fn=native<decltype(&XInputGetCapabilities)>("XInputGetCapabilities");
    return fn ? fn(index,flags,caps) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" void WINAPI FtdEnable(BOOL enabled) {
    auto fn=native<void (WINAPI*)(BOOL)>("XInputEnable"); if (fn) fn(enabled);
}
extern "C" DWORD WINAPI FtdGetBattery(DWORD index,BYTE type,XINPUT_BATTERY_INFORMATION* info) {
    if (!info || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{};
    if (virtualState(index,state)) { *info={BATTERY_TYPE_WIRED,BATTERY_LEVEL_FULL}; return ERROR_SUCCESS; }
    auto fn=native<decltype(&XInputGetBatteryInformation)>("XInputGetBatteryInformation");
    return fn ? fn(index,type,info) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI FtdGetKeystroke(DWORD index,DWORD reserved,XINPUT_KEYSTROKE* key) {
    if (!key || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{}; if (virtualState(index,state)) { *key={}; return ERROR_EMPTY; }
    auto fn=native<decltype(&XInputGetKeystroke)>("XInputGetKeystroke");
    return fn ? fn(index,reserved,key) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI FtdGetAudio(DWORD index,WCHAR* render,UINT* renderSize,WCHAR* capture,UINT* captureSize) {
    auto fn=native<decltype(&XInputGetAudioDeviceIds)>("XInputGetAudioDeviceIds");
    return fn ? fn(index,render,renderSize,capture,captureSize) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI FtdGetDSound(DWORD index,GUID* render,GUID* capture) {
    if (!render || !capture || index>3) return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE state{}; if (virtualState(index,state)) { *render={}; *capture={}; return ERROR_SUCCESS; }
    return ERROR_DEVICE_NOT_CONNECTED;
}
