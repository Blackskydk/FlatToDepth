// The SteamVR helper (src/steamvr_settings.hpp): finding SteamVR, and when the game-theater setting is turned off and put back.
// Nothing here changes SteamVR; with SteamVR running, the setting is only read.
#include "steamvr_settings.hpp"
#include <iostream>

static int failures=0;
#define CHECK(condition,message) do { if (!(condition)) { std::cerr<<"FAIL: "<<message<<"\n"; ++failures; } } while (0)

int main() {
    // openvrpaths.vrpath, as SteamVR writes it.
    const std::string paths=
        "{\n   \"config\" : [ \"C:\\\\Program Files (x86)\\\\Steam\\\\config\" ],\n   \"external_drivers\" : null,\n"
        "   \"jsonid\" : \"vrpathreg\",\n   \"log\" : [ \"C:\\\\Program Files (x86)\\\\Steam\\\\logs\" ],\n"
        "   \"runtime\" : [ \"C:\\\\Program Files (x86)\\\\Steam\\\\steamapps\\\\common\\\\SteamVR\" ],\n   \"version\" : 1\n}\n";
    CHECK(steamvr::runtimeFromPaths(paths)=="C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR","the runtime folder is read from openvrpaths.vrpath, escapes undone");
    CHECK(steamvr::runtimeFromPaths("{ \"runtime\" : [ \"D:\\\\VR\\\\SteamVR\", \"E:\\\\other\" ] }")=="D:\\VR\\SteamVR","the first runtime is the one used");
    CHECK(steamvr::runtimeFromPaths("{ \"config\" : [ \"C:\\\\x\" ] }").empty() && steamvr::runtimeFromPaths("").empty() && steamvr::runtimeFromPaths("{ \"runtime\" : [ ] }").empty() && steamvr::runtimeFromPaths("{ \"runtime\" : null }").empty(),
        "no runtime, no answer");

    // When the setting is changed and when it is put back.
    CHECK(steamvr::shouldTurnOff(true) && !steamvr::shouldTurnOff(false),"only a setting that is on is switched off: an off one is the player's own choice");
    CHECK(steamvr::shouldRestore(true,false),"put back: we turned it off and it is still off");
    CHECK(!steamvr::shouldRestore(true,true),"left alone: the player has turned it on again meanwhile");
    CHECK(!steamvr::shouldRestore(false,false) && !steamvr::shouldRestore(false,true),"left alone: it was not us");

    // The record that says the setting is ours to put back.
    {
        const auto folder=std::filesystem::temp_directory_path()/"flattodepth-steamvr-test";
        std::error_code ec; std::filesystem::remove_all(folder,ec);
        const auto record=folder/"state"/"steamvr-theater.txt";
        CHECK(!steamvr::recordExists(record),"no record at first");
        steamvr::writeRecord(record);
        CHECK(steamvr::recordExists(record),"a record is written, with its folder");
        steamvr::removeRecord(record); steamvr::removeRecord(record);
        CHECK(!steamvr::recordExists(record),"and removed (twice is fine)");
        steamvr::Settings notConnected; std::string note;
        steamvr::writeRecord(record);
        CHECK(steamvr::restoreTheater(notConnected,record,&note)==false && steamvr::recordExists(record),"with no connection nothing is restored and the record stays for the next run");
        steamvr::removeRecord(record);
        CHECK(steamvr::restoreTheater(notConnected,record,&note) && note=="nothing to restore","without a record there is nothing to do");
        std::filesystem::remove_all(folder,ec);
    }

    // With SteamVR running (a developer's PC): the setting can be read. It is not changed here.
    {
        steamvr::Settings settings; std::string why;
        if (settings.open(&why)) {
            const auto value=settings.getBool(steamvr::TheaterSection,steamvr::TheaterKey);
            CHECK(value.has_value(),"SteamVR's dashboard.autoShowGameTheater can be read");
            std::cout<<"SteamVR is running: dashboard.autoShowGameTheater = "<<(value ? (*value ? "on" : "off") : "unreadable")<<"\n";
        } else std::cout<<"SKIP live SteamVR read: "<<why<<"\n";
    }
    std::cout<<(failures ? "FAILED" : "PASS")<<" SteamVR helper\n";
    return failures ? 1 : 0;
}
