// The virtual Xbox controller (src/virtual_pad.hpp). The protocol numbers are pinned here; and on a PC that has the ViGEmBus
// driver, a pad is really plugged in and read back through Windows' own XInput, the way a game reads it. Where the driver is
// not installed (CI) that part is skipped, never failed.
#include "virtual_pad.hpp"
#include <chrono>
#include <iostream>
#include <thread>

static int failures=0;
#define CHECK(condition,message) do { if (!(condition)) { std::cerr<<"FAIL: "<<message<<"\n"; ++failures; } } while (0)

using GetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
using SetStateFn=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);

// Looks for the pad we made among XInput's four slots (another real pad may be plugged in too): the one whose reading is ours. A pad\n// that is given is fed the state on every look, the way the frame loop feeds it.
static int findSlot(GetStateFn get,const XINPUT_GAMEPAD& wanted,int milliseconds,VirtualPad* feed=nullptr) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    do {
        if (feed) feed->submit(wanted);
        for (DWORD slot=0;slot<4;++slot) {
            XINPUT_STATE state{};
            if (get(slot,&state)==ERROR_SUCCESS && !std::memcmp(&state.Gamepad,&wanted,sizeof(wanted))) return static_cast<int>(slot);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    } while (std::chrono::steady_clock::now()<until);
    return -1;
}

// Waits until the pad reports these motor speeds, as it would to a game that has just set its vibration.
static bool waitRumble(VirtualPad& pad,int heavy,int light,int milliseconds) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    do {
        const auto r=pad.rumble();
        if (r.heavy==heavy && r.light==light) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now()<until);
    return false;
}
// True once no slot shows that state any more: removing a pad takes Windows a moment.
static bool goneWithin(GetStateFn get,const XINPUT_GAMEPAD& state,int milliseconds) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    do {
        if (findSlot(get,state,0)<0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now()<until);
    return false;
}

int main() {
    // The driver's interface, as ViGEmBus 1.17 documents it: a write-access buffered request in the bus-extender device type.
    CHECK(vigem::PlugIn==0x002AA004 && vigem::Unplug==0x002AA008 && vigem::CheckVersion==0x002AA00C && vigem::WaitReady==0x002AA010 && vigem::SubmitXbox360==0x002AA808,
        "the request numbers are the driver's");
    CHECK(vigem::BusInterface.Data1==0x96E42B22 && vigem::BusInterface.Data2==0xF5E9 && vigem::BusInterface.Data3==0x42F8 && vigem::BusInterface.Data4[7]==0x4F,"the bus interface GUID");

    // Without a pad made, nothing happens and nothing breaks.
    { VirtualPad idle; idle.submit({}); idle.unplug(); CHECK(!idle.plugged(),"a pad that was never plugged in stays out"); }

    if (!VirtualPad::driverInstalled()) {
        std::cout<<"SKIP virtual pad on the system: the ViGEmBus driver is not installed here\n";
        std::cout<<(failures ? "FAILED" : "PASS")<<" virtual pad protocol\n";
        return failures ? 1 : 0;
    }

    HMODULE xinput=LoadLibraryW(L"xinput1_4.dll");
    CHECK(xinput!=nullptr,"the system XInput loads");
    if (!xinput) return 1;
    const auto get=reinterpret_cast<GetStateFn>(GetProcAddress(xinput,"XInputGetState"));
    CHECK(get!=nullptr,"XInputGetState is there");
    if (!get) return 1;

    XINPUT_GAMEPAD first{};
    first.wButtons=XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_RIGHT_SHOULDER; first.bLeftTrigger=123; first.bRightTrigger=45;
    first.sThumbLX=12345; first.sThumbLY=-23456; first.sThumbRX=-3210; first.sThumbRY=30000;
    XINPUT_GAMEPAD second=first; second.wButtons=XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_START; second.sThumbLX=-20000; second.bLeftTrigger=0;
    {
        VirtualPad pad; std::string why;
        CHECK(pad.plugIn(&why) && pad.plugged(),"a pad plugs in ("+why+")");
        CHECK(pad.plugIn(&why),"plugging in twice is harmless");
        const auto plugged=std::chrono::steady_clock::now();
        const int slot=findSlot(get,first,12000,&pad);
        std::cout<<"virtual pad visible to XInput after "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-plugged).count()<<" ms (XInput was already loaded)\n";
        CHECK(slot>=0,"Windows' XInput shows the pad with exactly the buttons, triggers and sticks that were sent");
        pad.submit(first); pad.submit(second);
        CHECK(findSlot(get,second,3000,&pad)>=0,"a new report replaces the old one");
        CHECK(findSlot(get,first,0)<0,"and the old state is gone");
        pad.submit({});
        CHECK(findSlot(get,XINPUT_GAMEPAD{},3000,&pad)>=0,"a neutral report releases everything");
        // Rumble: the game sets vibration through XInput, and the program is told the two motor speeds.
        const auto setVibration=reinterpret_cast<SetStateFn>(GetProcAddress(xinput,"XInputSetState"));
        CHECK(setVibration!=nullptr && slot>=0,"XInputSetState is there to play a game's part");
        if (setVibration && slot>=0) {
            CHECK(waitRumble(pad,0,0,500),"no rumble at first");
            XINPUT_VIBRATION v{0x8000,0x4000}; setVibration(static_cast<DWORD>(slot),&v);
            CHECK(waitRumble(pad,0x80,0x40,3000),"the game's rumble reaches the program: heavy motor 0x80, light motor 0x40");
            CHECK(pad.rumbleEvents()>=1,"and is counted");
            v={0xffff,0}; setVibration(static_cast<DWORD>(slot),&v);
            CHECK(waitRumble(pad,0xff,0,3000),"a new setting replaces the old: heavy motor full, light off");
            v={0,0}; setVibration(static_cast<DWORD>(slot),&v);
            CHECK(waitRumble(pad,0,0,3000),"and the game stopping it reaches the program");
        }
        std::cout<<"virtual pad: slot "<<slot<<", "<<pad.submitErrors()<<" reports answered with an error"<<(pad.submitErrors() ? " (last "+std::to_string(pad.lastSubmitError())+")" : "")<<"\n";
        pad.submit(first);
        pad.unplug();
        CHECK(!pad.plugged(),"unplugged");
    }
    // The pad is gone from Windows once unplugged (give the driver a moment).
    CHECK(goneWithin(get,first,5000),"the pad is gone from Windows after unplugging");
    // And the same object can plug in again.
    { VirtualPad again; std::string why; CHECK(again.plugIn(&why),"a pad can be made again ("+why+")"); CHECK(findSlot(get,second,6000,&again)>=0,"and works"); }
    FreeLibrary(xinput);
    std::cout<<(failures ? "FAILED" : "PASS")<<" virtual pad (ViGEmBus present)\n";
    return failures ? 1 : 0;
}
