#pragma once
#include <windows.h>
#include <setupapi.h>
#include <winioctl.h>
#include <xinput.h>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

// A virtual Xbox 360 controller, made through ViGEmBus: the signed bus driver that DS4Windows, many emulators and streaming
// tools already use. Windows shows such a pad to every game like a real one (XInput, Windows.Gaming.Input, Raw Input), so it
// reaches games that never call the XInput DLL a shim could stand in for (Rewired, Unity's Input System). FlatToDepth installs
// nothing: the driver is the user's to have, and without it a game's entry simply falls back to no controller.
//
// Only the driver's small device interface is used, spoken directly (the numbers were checked against ViGEmBus 1.17, the
// last release, and are pinned by tests/virtual_pad_test.cpp). The driver removes a pad when the process that made it ends.
namespace vigem {
constexpr GUID BusInterface{0x96E42B22,0xF5E9,0x42F8,{0xB0,0x43,0xED,0x0F,0x93,0x2F,0x01,0x4F}};
constexpr ULONG CommonVersion=1;
constexpr ULONG BaseFunction=0x801;
constexpr DWORD code(ULONG index) { return CTL_CODE(FILE_DEVICE_BUS_EXTENDER,BaseFunction+index,METHOD_BUFFERED,FILE_WRITE_ACCESS); }
constexpr DWORD PlugIn=code(0x000),Unplug=code(0x001),CheckVersion=code(0x002),WaitReady=code(0x003),SubmitXbox360=code(0x201);
// The one request that is held open: it completes when the game sets the pad's vibration, and carries the two motor speeds back.
constexpr DWORD RumbleNotification=CTL_CODE(FILE_DEVICE_BUS_EXTENDER,BaseFunction+0x200,METHOD_BUFFERED,FILE_READ_ACCESS|FILE_WRITE_ACCESS);
constexpr ULONG Xbox360Wired=0;
constexpr ULONG MaxTargets=16;
struct CheckVersionRequest { ULONG size,version; };
struct PlugInRequest { ULONG size,serial,type; USHORT vendor,product; };
struct SerialRequest { ULONG size,serial; };           // unplugging, and waiting for a plugged-in pad to be ready
struct Xbox360Report { USHORT buttons; BYTE leftTrigger,rightTrigger; SHORT leftX,leftY,rightX,rightY; };
struct SubmitRequest { ULONG size,serial; Xbox360Report report; };
struct RumbleRequest { ULONG size,serial; BYTE largeMotor,smallMotor,led; };   // the driver's own field names
static_assert(sizeof(CheckVersionRequest)==8 && sizeof(PlugInRequest)==16 && sizeof(SerialRequest)==8 && sizeof(SubmitRequest)==20 && sizeof(RumbleRequest)==12);
static_assert(sizeof(Xbox360Report)==sizeof(XINPUT_GAMEPAD),"an XInput pad is laid out exactly like the driver's Xbox 360 report");
}

class VirtualPad {
    HANDLE bus_=INVALID_HANDLE_VALUE,event_=nullptr;
    ULONG serial_=0;
    XINPUT_GAMEPAD last_{};
    bool sent_=false;
    std::chrono::steady_clock::time_point sentAt_{};
    unsigned submitErrors_=0;
    DWORD lastSubmitError_=0;
    // Rumble: one request stays pending with the driver; when it completes the game has set the motors, and the next one is made.
    OVERLAPPED rumbleIo_{};
    vigem::RumbleRequest rumbleBuffer_{};
    bool rumblePending_=false;
    BYTE heavyMotor_=0,lightMotor_=0;
    unsigned rumbleEvents_=0;

    static std::wstring busPath() {
        std::wstring path;
        HDEVINFO set=SetupDiGetClassDevsW(&vigem::BusInterface,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
        if (set==INVALID_HANDLE_VALUE) return path;
        SP_DEVICE_INTERFACE_DATA data{sizeof(data)};
        if (SetupDiEnumDeviceInterfaces(set,nullptr,&vigem::BusInterface,0,&data)) {
            DWORD need=0; SetupDiGetDeviceInterfaceDetailW(set,&data,nullptr,0,&need,nullptr);
            std::vector<char> buffer(need);
            auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data()); detail->cbSize=sizeof(*detail);
            if (need>=sizeof(*detail) && SetupDiGetDeviceInterfaceDetailW(set,&data,detail,need,&need,nullptr)) path=detail->DevicePath;
        }
        SetupDiDestroyDeviceInfoList(set);
        return path;
    }
    // One request to the bus. A request the driver holds (waiting for a pad to be ready) is given timeoutMs, then cancelled,
    // so a stuck driver can never freeze the frame loop. Returns the Win32 error, 0 for success.
    DWORD call(DWORD control,void* in,DWORD size,DWORD timeoutMs) {
        OVERLAPPED o{}; o.hEvent=event_; ResetEvent(event_);
        DWORD got=0;
        if (DeviceIoControl(bus_,control,in,size,nullptr,0,&got,&o)) return 0;
        DWORD error=GetLastError();
        if (error!=ERROR_IO_PENDING) return error;
        if (GetOverlappedResultEx(bus_,&o,&got,timeoutMs,FALSE)) return 0;
        error=GetLastError();
        if (error==WAIT_TIMEOUT) { CancelIoEx(bus_,&o); GetOverlappedResult(bus_,&o,&got,TRUE); }
        return error;
    }
    // Makes sure a rumble request is waiting with the driver, taking in any that has completed.
    void serviceRumble() {
        for (int rounds=0;rounds<8 && plugged();++rounds) {
            if (rumblePending_) {
                DWORD got=0;
                if (!GetOverlappedResult(bus_,&rumbleIo_,&got,FALSE)) { if (GetLastError()!=ERROR_IO_INCOMPLETE) rumblePending_=false; return; }   // still waiting, or failed (then try again next time)
                rumblePending_=false;
                heavyMotor_=rumbleBuffer_.largeMotor; lightMotor_=rumbleBuffer_.smallMotor; ++rumbleEvents_;
            }
            rumbleBuffer_={sizeof(rumbleBuffer_),serial_,0,0,0};
            rumbleIo_={}; rumbleIo_.hEvent=rumbleEvent_; ResetEvent(rumbleEvent_);
            DWORD got=0;
            if (DeviceIoControl(bus_,vigem::RumbleNotification,&rumbleBuffer_,sizeof(rumbleBuffer_),&rumbleBuffer_,sizeof(rumbleBuffer_),&got,&rumbleIo_)) { rumblePending_=true; continue; }   // done at once: collect it on the next round
            if (GetLastError()==ERROR_IO_PENDING) { rumblePending_=true; return; }
            return;                                    // the driver refused: no rumble, nothing else is affected
        }
    }
    void cancelRumble() {
        if (rumblePending_ && bus_!=INVALID_HANDLE_VALUE) { CancelIoEx(bus_,&rumbleIo_); DWORD got=0; GetOverlappedResult(bus_,&rumbleIo_,&got,TRUE); }
        rumblePending_=false; heavyMotor_=lightMotor_=0;
    }
    void closeBus() {
        if (bus_!=INVALID_HANDLE_VALUE) CloseHandle(bus_);
        bus_=INVALID_HANDLE_VALUE;
        if (event_) CloseHandle(event_);
        event_=nullptr;
        if (rumbleEvent_) CloseHandle(rumbleEvent_);
        rumbleEvent_=nullptr;
    }
    HANDLE rumbleEvent_=nullptr;
public:
    VirtualPad()=default;
    VirtualPad(const VirtualPad&)=delete;
    VirtualPad& operator=(const VirtualPad&)=delete;
    ~VirtualPad() { unplug(); }
    // Is the driver there at all? (Cheap: asks Windows, opens nothing.)
    static bool driverInstalled() { return !busPath().empty(); }
    bool plugged() const { return serial_!=0; }
    // Makes the pad appear in Windows. True when it is there afterwards; otherwise `why` says what is missing.
    bool plugIn(std::string* why=nullptr) {
        if (plugged()) return true;
        auto fail=[&](const std::string& text) { if (why) *why=text; unplug(); return false; };
        const std::wstring path=busPath();
        if (path.empty()) return fail("the ViGEmBus driver (a virtual gamepad bus) is not installed");
        bus_=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED,nullptr);
        if (bus_==INVALID_HANDLE_VALUE) return fail("the ViGEmBus driver could not be opened (Windows error "+std::to_string(GetLastError())+")");
        event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        rumbleEvent_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        vigem::CheckVersionRequest version{sizeof(version),vigem::CommonVersion};
        if (DWORD error=call(vigem::CheckVersion,&version,sizeof(version),2000)) return fail("the ViGEmBus driver does not speak the version FlatToDepth expects (Windows error "+std::to_string(error)+")");
        // The first free slot: another program may already have made pads of its own.
        DWORD error=ERROR_NO_MORE_ITEMS;
        for (ULONG serial=1;serial<=vigem::MaxTargets;++serial) {
            vigem::PlugInRequest request{sizeof(request),serial,vigem::Xbox360Wired,0,0};
            error=call(vigem::PlugIn,&request,sizeof(request),2000);
            if (!error) { serial_=serial; break; }
        }
        if (!serial_) return fail("ViGEmBus has no free slot for another pad (Windows error "+std::to_string(error)+")");
        vigem::SerialRequest ready{sizeof(ready),serial_};
        call(vigem::WaitReady,&ready,sizeof(ready),5000);     // an older driver does not know this request; the pad works without it
        sent_=false; submitErrors_=0; heavyMotor_=lightMotor_=0; rumbleEvents_=0;
        submit(XINPUT_GAMEPAD{});
        serviceRumble();
        return true;
    }
    // Sets what the pad reports. The state is sent when it changes, and again a few times a second while it does not: the first reports after
    // a pad appears can be lost before Windows is reading it, and a lost report would otherwise stay lost.
    void submit(const XINPUT_GAMEPAD& pad) {
        const auto now=std::chrono::steady_clock::now();
        if (!plugged() || (sent_ && !std::memcmp(&last_,&pad,sizeof(pad)) && now-sentAt_<std::chrono::milliseconds(250))) return;
        vigem::SubmitRequest request{sizeof(request),serial_,{}};
        std::memcpy(&request.report,&pad,sizeof(pad));
        const DWORD error=call(vigem::SubmitXbox360,&request,sizeof(request),100);
        if (error==ERROR_ACCESS_DENIED) { serial_=0; return; }   // the pad was removed from under us (the driver was restarted)
        if (error) { ++submitErrors_; lastSubmitError_=error; }
        last_=pad; sent_=true; sentAt_=now;
    }
    // What the game last asked of the pad's two motors (0 to 255 each: the large, low-frequency one and the small one), and how many times it asked.
    // Call every frame: it collects the driver's answer and makes the next request.
    struct Rumble { BYTE heavy,light; };      // the large (low-frequency) motor and the small one
    Rumble rumble() { serviceRumble(); return {heavyMotor_,lightMotor_}; }
    unsigned rumbleEvents() const { return rumbleEvents_; }
    unsigned submitErrors() const { return submitErrors_; }       // reports the driver answered with an error, for the log
    DWORD lastSubmitError() const { return lastSubmitError_; }
    void unplug() {
        cancelRumble();
        if (serial_ && bus_!=INVALID_HANDLE_VALUE) { vigem::SerialRequest request{sizeof(request),serial_}; call(vigem::Unplug,&request,sizeof(request),2000); }
        serial_=0; sent_=false;
        closeBus();
    }
};
