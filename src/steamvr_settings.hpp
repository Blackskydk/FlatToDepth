#pragma once
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <string>

// Keeps SteamVR from putting the flat game's own window in front of FlatToDepth.
//
// When a flat (non-VR) game starts while SteamVR is running, SteamVR opens a "game theater": the game's window on a big virtual
// screen, in front of whatever VR application is showing, and that takes VR input focus from it, which is why FlatToDepth's
// screen is hidden and the controllers stop reaching the game. SteamVR has a setting for it, dashboard.autoShowGameTheater, which
// is on by default. A small helper process (FlatToDepth.exe --steamvr-theater-guard) turns it off while FlatToDepth runs and puts
// it back when FlatToDepth ends, even if that is by a crash: a record file says whether the setting is ours to put back.
//
// This is a separate process, never FlatToDepth's own OpenXR process: it connects to SteamVR as a "utility" client (the kind that
// only reads and writes settings) through the openvr_api.dll that ships with SteamVR itself, so nothing is bundled or downloaded.
namespace steamvr {

// The runtime folder out of %LOCALAPPDATA%\openvr\openvrpaths.vrpath, a small JSON file with  "runtime" : [ "C:\\...\\SteamVR" ].
inline std::string runtimeFromPaths(const std::string& json) {
    const auto key=json.find("\"runtime\"");
    if (key==std::string::npos) return {};
    const auto open=json.find('[',key);
    if (open==std::string::npos) return {};
    const auto first=json.find('"',open);
    if (first==std::string::npos) return {};
    std::string out;
    for (size_t i=first+1;i<json.size() && json[i]!='"';++i) {
        if (json[i]=='\\' && i+1<json.size()) ++i;       // \\ is one backslash; \/ is a slash
        out+=json[i];
    }
    return out;
}
inline std::filesystem::path runtimeFolder() {
    wchar_t local[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH)) return {};
    std::ifstream in(std::filesystem::path(local)/L"openvr"/L"openvrpaths.vrpath",std::ios::binary);
    if (!in) return {};
    const std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    const std::string runtime=runtimeFromPaths(text);
    return runtime.empty() ? std::filesystem::path() : std::filesystem::path(runtime);
}

// What to do with the setting, as plain decisions so they can be tested without SteamVR.
// At the start: only a setting that is on is switched off (an off setting is the player's own choice, or already ours).
inline bool shouldTurnOff(bool current) { return current; }
// At the end: put it back only if we turned it off (there is a record) and it is still off (the player has not turned it on since).
inline bool shouldRestore(bool haveRecord,bool current) { return haveRecord && !current; }

// SteamVR's settings, through its OpenVR client library, as a utility application.
class Settings {
    struct Table {   // openvr_capi.h: VR_IVRSettings_FnTable, in this order
        const char* (__cdecl* errorName)(int);
        bool (__cdecl* sync)(bool,int*);
        void (__cdecl* setBool)(const char*,const char*,bool,int*);
        void (__cdecl* setInt32)(const char*,const char*,int32_t,int*);
        void (__cdecl* setFloat)(const char*,const char*,float,int*);
        void (__cdecl* setString)(const char*,const char*,const char*,int*);
        bool (__cdecl* getBool)(const char*,const char*,int*);
        int32_t (__cdecl* getInt32)(const char*,const char*,int*);
        float (__cdecl* getFloat)(const char*,const char*,int*);
        void (__cdecl* getString)(const char*,const char*,char*,uint32_t,int*);
        void (__cdecl* removeSection)(const char*,int*);
        void (__cdecl* removeKey)(const char*,const char*,int*);
    };
    HMODULE dll_=nullptr;
    void (__cdecl* shutdown_)()=nullptr;
    Table* table_=nullptr;
public:
    Settings()=default;
    Settings(const Settings&)=delete;
    Settings& operator=(const Settings&)=delete;
    ~Settings() { close(); }
    bool connected() const { return table_!=nullptr; }
    // Connects to the running SteamVR. False (with the reason) if SteamVR is not running, or not installed.
    bool open(std::string* why=nullptr) {
        if (connected()) return true;
        auto fail=[&](const std::string& text) { if (why) *why=text; close(); return false; };
        const auto runtime=runtimeFolder();
        if (runtime.empty()) return fail("SteamVR's location is not recorded (no openvrpaths.vrpath)");
        const auto bin=runtime/L"bin"/L"win64";
        SetDllDirectoryW(bin.c_str());
        dll_=LoadLibraryW((bin/L"openvr_api.dll").c_str());
        if (!dll_) return fail("openvr_api.dll was not found in "+bin.string());
        const auto init=reinterpret_cast<uint32_t(__cdecl*)(int*,int)>(GetProcAddress(dll_,"VR_InitInternal"));
        const auto generic=reinterpret_cast<void*(__cdecl*)(const char*,int*)>(GetProcAddress(dll_,"VR_GetGenericInterface"));
        shutdown_=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(dll_,"VR_ShutdownInternal"));
        if (!init || !generic || !shutdown_) return fail("openvr_api.dll does not look like OpenVR's client library");
        int error=0;
        init(&error,4);                       // VRApplication_Utility: settings and applications only, no headset
        if (error) { shutdown_=nullptr; return fail("SteamVR is not running (OpenVR error "+std::to_string(error)+")"); }
        int tableError=0;
        table_=static_cast<Table*>(generic("FnTable:IVRSettings_003",&tableError));
        if (!table_) return fail("this SteamVR does not offer the settings interface FlatToDepth expects");
        return true;
    }
    std::optional<bool> getBool(const char* section,const char* key) {
        if (!table_) return std::nullopt;
        int error=0; const bool value=table_->getBool(section,key,&error);
        if (error) return std::nullopt;
        return value;
    }
    bool setBool(const char* section,const char* key,bool value) {
        if (!table_) return false;
        int error=0; table_->setBool(section,key,value,&error);
        if (error) return false;
        return getBool(section,key)==value;     // no forced Sync: SteamVR keeps the value, and calling it from a utility client crashed

    }
    void close() {
        table_=nullptr;
        if (shutdown_) shutdown_();
        shutdown_=nullptr;
        if (dll_) FreeLibrary(dll_);
        dll_=nullptr;
    }
};

constexpr const char* TheaterSection="dashboard";
constexpr const char* TheaterKey="autoShowGameTheater";

inline bool recordExists(const std::filesystem::path& record) { std::error_code ec; return std::filesystem::exists(record,ec); }
inline void writeRecord(const std::filesystem::path& record) {
    std::error_code ec; std::filesystem::create_directories(record.parent_path(),ec);
    std::ofstream(record)<<"FlatToDepth turned SteamVR's dashboard.autoShowGameTheater off. It puts it back when it ends; if it did not (a crash), the next run does.\n";
}
inline void removeRecord(const std::filesystem::path& record) { std::error_code ec; std::filesystem::remove(record,ec); }

// Puts the setting back if FlatToDepth turned it off. True when there is nothing left to put back.
inline bool restoreTheater(Settings& settings,const std::filesystem::path& record,std::string* note=nullptr) {
    if (!recordExists(record)) { if (note) *note="nothing to restore"; return true; }
    const auto current=settings.getBool(TheaterSection,TheaterKey);
    if (!current) { if (note) *note="could not read the setting"; return false; }
    if (shouldRestore(true,*current)) {
        if (!settings.setBool(TheaterSection,TheaterKey,true)) { if (note) *note="could not put the setting back on"; return false; }
        if (note) *note="put autoShowGameTheater back on";
    } else if (note) *note="autoShowGameTheater was already on again";
    removeRecord(record);
    return true;
}

inline void guardLog(const std::filesystem::path& file,const std::string& text) {
    std::error_code ec; std::filesystem::create_directories(file.parent_path(),ec);
    std::ofstream out(file,std::ios::app);
    const std::time_t now=std::time(nullptr); std::tm local{}; localtime_s(&local,&now);
    out<<std::put_time(&local,"%Y-%m-%d %H:%M:%S")<<"  "<<text<<"\n";
}

// FlatToDepth.exe --steamvr-theater-guard <FlatToDepth's process id> <record file> <log file>
// Waits for SteamVR, turns the game theater off, waits for FlatToDepth to end, turns it back on.
inline int runTheaterGuard(DWORD parentPid,const std::filesystem::path& record,const std::filesystem::path& logPath) {
    HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,parentPid);
    if (!parent) { guardLog(logPath,"FlatToDepth (process "+std::to_string(parentPid)+") is not running; nothing to guard"); return 1; }
    auto parentEnded=[&](DWORD milliseconds) { return WaitForSingleObject(parent,milliseconds)==WAIT_OBJECT_0; };
    int result=0;
    {
        Settings settings; std::string why; bool connected=false,reported=false;
        // FlatToDepth waits for SteamVR as well, so this may take a while; give up when FlatToDepth does.
        for (int attempt=0;attempt<200 && !connected;++attempt) {
            if (parentEnded(0)) { CloseHandle(parent); return 0; }
            connected=settings.open(&why);
            if (!connected) { if (!reported) { guardLog(logPath,"waiting for SteamVR: "+why); reported=true; } parentEnded(3000); }
        }
        if (!connected) { guardLog(logPath,"gave up waiting for SteamVR"); CloseHandle(parent); return 1; }
        guardLog(logPath,"connected to SteamVR");
        const auto current=settings.getBool(TheaterSection,TheaterKey);
        if (!current) { guardLog(logPath,"could not read dashboard.autoShowGameTheater; leaving SteamVR alone"); result=1; }
        else if (shouldTurnOff(*current)) {
            writeRecord(record);
            if (settings.setBool(TheaterSection,TheaterKey,false)) guardLog(logPath,"turned SteamVR's autoShowGameTheater off, so a flat game does not cover FlatToDepth");
            else { guardLog(logPath,"could not turn autoShowGameTheater off"); removeRecord(record); result=1; }
        } else guardLog(logPath,std::string("autoShowGameTheater is already off")+(recordExists(record) ? " (by an earlier FlatToDepth run; it will be put back when this one ends)" : " (your own choice); left as it is"));
    }
    parentEnded(INFINITE);
    CloseHandle(parent);
    // FlatToDepth has ended. SteamVR may be closing too, so try for a while.
    for (int attempt=0;attempt<10;++attempt) {
        Settings settings; std::string note;
        if (settings.open(&note) && restoreTheater(settings,record,&note)) { guardLog(logPath,note); return result; }
        if (!recordExists(record)) return result;
        Sleep(1500);
    }
    guardLog(logPath,"could not reach SteamVR to put autoShowGameTheater back; the next FlatToDepth run will");
    return result;
}

// FlatToDepth.exe --steamvr-theater-restore <record file> <log file>: puts the setting back by hand (for Uninstall).
inline int runTheaterRestore(const std::filesystem::path& record,const std::filesystem::path& logPath) {
    if (!recordExists(record)) { std::printf("SteamVR's game theater setting was not changed by FlatToDepth.\n"); return 0; }
    Settings settings; std::string note;
    if (settings.open(&note) && restoreTheater(settings,record,&note)) { guardLog(logPath,note+" (by hand)"); std::printf("SteamVR's game theater setting: %s.\n",note.c_str()); return 0; }
    std::printf("Could not reach SteamVR (%s). Start SteamVR and run this again, or turn the theater back on in SteamVR's settings.\n",note.c_str());
    return 1;
}

// Starts the guard for the running FlatToDepth, if its setting asks for it. Never fatal: the guard is a courtesy.
inline void startTheaterGuard(const std::filesystem::path& record,const std::filesystem::path& logPath) {
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr,exe,MAX_PATH)) return;
    std::wstring command=L"\""+std::wstring(exe)+L"\" --steamvr-theater-guard "+std::to_wstring(GetCurrentProcessId())+L" \""+record.wstring()+L"\" \""+logPath.wstring()+L"\"";
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION info{};
    // Outside FlatToDepth's job, if it is in one that allows that: the guard has to outlive a crash to put the setting back.
    BOOL started=CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_BREAKAWAY_FROM_JOB,nullptr,nullptr,&startup,&info);
    if (!started) started=CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info);
    if (started) { CloseHandle(info.hThread); CloseHandle(info.hProcess); }
}
}
