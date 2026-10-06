#include <windows.h>
#include <xinput.h>
#include <cstring>
#include <iostream>
#include <string>
#include <stdexcept>
#include "gamepad_ipc.hpp"
int main(int argc,char** argv) {
    try {
        // --dll name loads a differently named copy of the shim, as the 64-bit build is shipped under XInput1_4/1_3.
        std::wstring name=L"xinput9_1_0.dll";
        for (int i=1;i+1<argc;++i) if (std::strcmp(argv[i],"--dll")==0) { const std::string n=argv[i+1]; name.assign(n.begin(),n.end()); }
        auto dll=LoadLibraryW(name.c_str()); if (!dll) throw std::runtime_error("Load local shim failed");
        auto get=reinterpret_cast<decltype(&XInputGetState)>(GetProcAddress(dll,"XInputGetState"));
        auto caps=reinterpret_cast<decltype(&XInputGetCapabilities)>(GetProcAddress(dll,"XInputGetCapabilities"));
        auto set=reinterpret_cast<decltype(&XInputSetState)>(GetProcAddress(dll,"XInputSetState"));
        if (!get || !caps || !set) throw std::runtime_error("Shim exports missing");
        XINPUT_STATE state{};
        if (argc>1 && std::strcmp(argv[1],"--watch")==0) {
            for (int i=0;i<300;++i) {
                DWORD result=get(0,&state);
                std::cout<<"XInput="<<result<<" buttons="<<state.Gamepad.wButtons<<" LX="<<state.Gamepad.sThumbLX<<" LY="<<state.Gamepad.sThumbLY<<std::endl;
                Sleep(100);
            }
        } else {
            if (get(0,&state)!=ERROR_SUCCESS || state.Gamepad.wButtons!=XINPUT_GAMEPAD_A || state.Gamepad.sThumbLX!=12345 || state.Gamepad.bRightTrigger!=200)
                throw std::runtime_error("publisher-to-shim gamepad state mismatch");
            XINPUT_CAPABILITIES c{};
            if (caps(0,0,&c)!=ERROR_SUCCESS || c.SubType!=XINPUT_DEVSUBTYPE_GAMEPAD) throw std::runtime_error("Virtual controller capabilities failed");
            if (get(4,&state)!=ERROR_BAD_ARGUMENTS || get(0,nullptr)!=ERROR_BAD_ARGUMENTS) throw std::runtime_error("Invalid argument handling failed");
            // Rumble the game asks for must reach the bridge (the test checks the shared block after this exits).
            XINPUT_VIBRATION vibration{0x1234,0xabcd};
            if (set(0,&vibration)!=ERROR_SUCCESS || set(4,&vibration)!=ERROR_BAD_ARGUMENTS || set(0,nullptr)!=ERROR_BAD_ARGUMENTS)
                throw std::runtime_error("XInputSetState handling failed");
            std::cout<<"PASS shim receives x64 gamepad buttons/axes/triggers/capabilities and sends rumble back\n";
        }
        FreeLibrary(dll); return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
