#include "common.hpp"
#include "gamepad_ipc.hpp"
int main() {
    try {
        const std::wstring name=std::wstring(GamepadTestPrefix)+std::to_wstring(GetCurrentProcessId());
        SetEnvironmentVariableW(L"FLATTODEPTH_CONTROLLER_TEST_MAPPING",name.c_str());
        GamepadPublisher publisher(name.c_str());
        XINPUT_GAMEPAD pad{}; pad.wButtons=XINPUT_GAMEPAD_A; pad.sThumbLX=12345; pad.bRightTrigger=200;
        publisher.publish(pad,true);
        // Runs a shim test client from its own folder (so the shim next to it is the one it loads) and keeps the
        // virtual pad fresh meanwhile. Each run must leave the shim's read counter higher than before.
        auto runClient=[&](GamepadPublisher& publisher,const wchar_t* folder,std::wstring command,const char* what) {
            const LONG before=publisher.reads(), writesBefore=publisher.motorWrites();
            STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
            require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,folder,&si,&pi)!=0,
                (std::string("Could not launch ")+what+" (build it with scripts\\build-gamepad.cmd / build-gamepad64.cmd)").c_str());
            const auto start=GetTickCount64();
            while (WaitForSingleObject(pi.hProcess,10)==WAIT_TIMEOUT) {
                publisher.publish(pad,true); require(GetTickCount64()-start<10000,"gamepad client timeout");
            }
            DWORD code=1;
            GetExitCodeProcess(pi.hProcess,&code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            require(code==0,(std::string(what)+" rejected controller data").c_str());
            require(publisher.reads()>before,(std::string(what)+": shim read count not recorded").c_str());
            // The client set 0x1234 / 0xabcd through XInputSetState; the bridge must see exactly that, once.
            require(publisher.motors()==packMotors(0x1234,0xabcd),(std::string(what)+": rumble request did not reach the bridge").c_str());
            require(publisher.motorWrites()==writesBefore+1,(std::string(what)+": rumble write count wrong").c_str());
            publisher.clearMotors();
            require(publisher.motors()==0,"clearMotors did not stop the motors");
        };
        runClient(publisher,L"gamepad",L"gamepad\\FlatToDepthGamepadClient.exe","x86 shim (32-bit games)");
        // 64-bit games load XInput1_4, with older names as fallbacks.
        runClient(publisher,L"gamepad64",L"gamepad64\\FlatToDepthGamepadClient.exe --dll xinput1_4.dll","x64 shim as xinput1_4.dll");
        runClient(publisher,L"gamepad64",L"gamepad64\\FlatToDepthGamepadClient.exe --dll xinput1_3.dll","x64 shim as xinput1_3.dll");
        require(leftMotor(packMotors(0xffff,0))==0xffff && rightMotor(packMotors(0xffff,0))==0 &&
            leftMotor(packMotors(0,0x8001))==0 && rightMotor(packMotors(0,0x8001))==0x8001 &&
            leftMotor(packMotors(0x1234,0xabcd))==0x1234 && rightMotor(packMotors(0x1234,0xabcd))==0xabcd,"motor packing must round-trip the full 16 bits");
        // A mapping an older build left behind, kept open by a game that is still running: reusing it must not fail.
        {
            const auto legacyName=L"Local\\FlatToDepthGamepadOldSize_"+std::to_wstring(GetCurrentProcessId());
            HANDLE legacy=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(LegacyGamepadSize),legacyName.c_str());
            require(legacy!=nullptr,"Could not create a legacy-sized mapping");
            GamepadPublisher reused(legacyName.c_str());
            reused.publish(pad,true);
            if (!reused.rumbleSupported()) require(reused.motors()==0 && reused.motorWrites()==0,"A legacy mapping must report no rumble");
            reused.clearMotors();
            CloseHandle(legacy);
        }
        // The real mapping name, as the bridge uses it: the shim finds it with no test override. Skipped if a real bridge
        // is running here.
        SetEnvironmentVariableW(L"FLATTODEPTH_CONTROLLER_TEST_MAPPING",nullptr);
        HANDLE realBridge=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,GamepadMappingName);
        if (realBridge) { CloseHandle(realBridge); std::cout<<"SKIP real mapping name: a bridge is running on this PC\n"; }
        else {
            GamepadPublisher bridge;
            bridge.publish(pad,true);   // the pad is connected before the game starts, as in the runs above
            runClient(bridge,L"gamepad64",L"gamepad64\\FlatToDepthGamepadClient.exe --dll xinput1_4.dll","x64 shim, default mapping name");
        }
        SharedGamepad stale{}; stale.magic=GamepadMagic; stale.version=1; stale.connected=1; stale.heartbeat=10;
        XINPUT_STATE out{}; require(!readGamepad(&stale,out,1000),"Stale controller accepted");
        stale.sequence=1; stale.heartbeat=999; require(!readGamepad(&stale,out,1000),"Partial writer update accepted");
        std::cout<<"PASS controller transport to the x86 and x64 shims, stale/torn rejection\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
