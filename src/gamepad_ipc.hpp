#pragma once
#include <windows.h>
#include <xinput.h>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <stdexcept>

constexpr wchar_t GamepadMappingName[]=L"Local\\FlatToDepthGamepadV1";
// Names starting with this are accepted from the environment (FLATTODEPTH_CONTROLLER_TEST_MAPPING) so tests never touch
// the real mapping.
constexpr wchar_t GamepadTestPrefix[]=L"Local\\FlatToDepthGamepadTest_";
constexpr DWORD GamepadMagic=0x3156524f;
// Fixed-width ABI shared by the x64 OpenXR publisher and the x86/x64 XInput compatibility DLL.
struct alignas(8) SharedGamepad {
    volatile LONG sequence;
    DWORD magic,version,connected;
    ULONGLONG heartbeat;
    XINPUT_STATE state;
    volatile LONG readCount;
    DWORD readerPid;
    // Rumble the game asked for through XInputSetState, flowing the other way (shim to bridge). The low 16 bits are
    // the left (heavy) motor, the high 16 bits the right (light) motor. They sit after the original fields, so a
    // shim built before they existed, which maps only the first 48 bytes, keeps working.
    volatile LONG motors;
    volatile LONG motorWrites;      // how many times the game has set them
};
static_assert(sizeof(SharedGamepad)==56);
static_assert(offsetof(SharedGamepad,state)==24);
static_assert(offsetof(SharedGamepad,motors)==48);
// What an older build mapped: everything before the rumble fields. A game that is still running keeps the mapping an
// older bridge made, at that size, and a newer bridge or shim must cope with it (without rumble) rather than fail.
constexpr size_t LegacyGamepadSize=offsetof(SharedGamepad,motors);
inline LONG packMotors(WORD left,WORD right) { return static_cast<LONG>(static_cast<DWORD>(left) | (static_cast<DWORD>(right)<<16)); }
inline WORD leftMotor(LONG packed) { return static_cast<WORD>(static_cast<DWORD>(packed) & 0xffff); }
inline WORD rightMotor(LONG packed) { return static_cast<WORD>(static_cast<DWORD>(packed)>>16); }
inline bool readGamepad(SharedGamepad* shared,XINPUT_STATE& state,ULONGLONG now=GetTickCount64()) {
    for (int tries=0;tries<8;++tries) {
        const LONG before=InterlockedCompareExchange(&shared->sequence,0,0);
        if (before&1) continue;
        const DWORD magic=shared->magic,version=shared->version,connected=shared->connected;
        const ULONGLONG heartbeat=shared->heartbeat;
        XINPUT_STATE snapshot=shared->state;
        MemoryBarrier();
        if (before!=InterlockedCompareExchange(&shared->sequence,0,0)) continue;
        if (magic!=GamepadMagic || version!=1 || !connected || now<heartbeat || now-heartbeat>500) return false;
        state=snapshot; return true;
    }
    return false;
}
// One shared-memory mapping, written by the bridge and read by the shim inside the game.
class GamepadMapping {
    HANDLE mapping_=nullptr;
    SharedGamepad* shared_=nullptr;
    bool rumble_=true;     // false when the mapping in use is an older build's, which has no motor fields
public:
    explicit GamepadMapping(const wchar_t* name) {
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedGamepad),name);
        if (!mapping_) throw std::runtime_error("Create gamepad IPC mapping failed");
        shared_=static_cast<SharedGamepad*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedGamepad)));
        if (!shared_) {   // reusing a mapping an older build left behind: it works, but has no room for rumble
            shared_=static_cast<SharedGamepad*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,LegacyGamepadSize));
            rumble_=false;
        }
        if (!shared_) { CloseHandle(mapping_); throw std::runtime_error("Map gamepad IPC failed"); }
        // Reuse a mapping retained by the game across a bridge restart.
        InterlockedExchange(&shared_->sequence,0);
        clearMotors();
        publish(0,{},false);
    }
    ~GamepadMapping() { if (shared_) { publish(0,{},false); UnmapViewOfFile(shared_); } if (mapping_) CloseHandle(mapping_); }
    GamepadMapping(const GamepadMapping&)=delete;
    void publish(DWORD packet,const XINPUT_GAMEPAD& pad,bool connected) {
        InterlockedIncrement(&shared_->sequence);
        shared_->magic=GamepadMagic; shared_->version=1; shared_->connected=connected;
        shared_->heartbeat=GetTickCount64(); shared_->state={packet,pad};
        MemoryBarrier(); InterlockedIncrement(&shared_->sequence);
    }
    LONG reads() const { return InterlockedCompareExchange(&shared_->readCount,0,0); }
    DWORD readerPid() const { return shared_->readerPid; }
    LONG motors() const { return rumble_ ? InterlockedCompareExchange(&shared_->motors,0,0) : 0; }
    LONG motorWrites() const { return rumble_ ? InterlockedCompareExchange(&shared_->motorWrites,0,0) : 0; }
    void clearMotors() { if (rumble_) InterlockedExchange(&shared_->motors,0); }
    bool rumbleSupported() const { return rumble_; }
};
// Publishes the VR controllers as a gamepad, counting a new packet whenever the pad's state changes.
class GamepadPublisher {
    GamepadMapping primary_;
    DWORD packet_=0;
    XINPUT_GAMEPAD last_{};
public:
    explicit GamepadPublisher(const wchar_t* name=GamepadMappingName) : primary_(name) {}
    GamepadPublisher(const GamepadPublisher&)=delete;
    void publish(const XINPUT_GAMEPAD& pad,bool connected) {
        if (std::memcmp(&last_,&pad,sizeof(pad))) { ++packet_; last_=pad; }
        primary_.publish(packet_,pad,connected);
    }
    LONG reads() const { return primary_.reads(); }
    DWORD readerPid() const { return primary_.readerPid(); }
    // What the game last asked the rumble motors to do: bit-packed, see packMotors().
    LONG motors() const { return primary_.motors(); }
    LONG motorWrites() const { return primary_.motorWrites(); }
    // A finished or crashed game cannot turn its motors off, so the bridge does it for the next one.
    void clearMotors() { primary_.clearMotors(); }
    bool rumbleSupported() const { return primary_.rumbleSupported(); }
};
