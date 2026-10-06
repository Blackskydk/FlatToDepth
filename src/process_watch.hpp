#pragma once
#include "common.hpp"
#include <tlhelp32.h>

// True if any running process has this executable name (case-insensitive), e.g. L"oriDE.exe".
inline bool processRunning(const std::wstring& exeName) {
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snapshot==INVALID_HANDLE_VALUE) return true;     // cannot tell: do not report the game as gone
    PROCESSENTRY32W entry{sizeof(entry)};
    bool found=false;
    for (bool more=Process32FirstW(snapshot,&entry);more && !found;more=Process32NextW(snapshot,&entry))
        found=_wcsicmp(entry.szExeFile,exeName.c_str())==0;
    CloseHandle(snapshot);
    return found;
}

// True if the window that has the keyboard right now belongs to a process with this executable name.
inline bool foregroundIs(const std::wstring& exeName) {
    const HWND window=GetForegroundWindow();
    if (!window) return false;
    DWORD pid=0; GetWindowThreadProcessId(window,&pid);
    if (!pid) return false;
    const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!process) return false;
    wchar_t path[MAX_PATH]{}; DWORD size=MAX_PATH;
    const bool ok=QueryFullProcessImageNameW(process,0,path,&size)!=0;
    CloseHandle(process);
    return ok && _wcsicmp(std::filesystem::path(path).filename().c_str(),exeName.c_str())==0;
}

// Lets the bridge end together with the game it was started for (`--follow oriDE.exe`).
class ProcessFollower {
    std::wstring name_;
    bool seen_=false;
    std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now(),next_{};
public:
    explicit ProcessFollower(std::wstring name):name_(std::move(name)) {}
    bool enabled() const { return !name_.empty(); }
    // True once the process has gone away, or never showed up within a minute. Checks every two seconds.
    bool finished() {
        const auto now=std::chrono::steady_clock::now();
        if (!enabled() || now<next_) return false;
        next_=now+std::chrono::seconds(2);
        const bool alive=processRunning(name_);
        seen_|=alive;
        return !alive && (seen_ || now-start_>std::chrono::seconds(60));
    }
};
