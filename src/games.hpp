#pragma once
#include "catalog.hpp"
#include "process_watch.hpp"
#include <objbase.h>
#include <shellapi.h>
#include <iterator>
#include <regex>

// The games FlatToDepth can bridge, loaded at start-up from the catalog (see catalog.hpp): the shipped games.catalog.ini plus
// the user's games.user.ini. Empty until loadGames() runs.
inline std::vector<GameInfo> Games;

// Reads the catalog files next to the main config and logs anything wrong with them. Returns false if no game at all
// could be loaded.
inline bool loadGames(const std::filesystem::path& dir,std::vector<std::string>* problems=nullptr) {
    auto result=loadCatalogFiles(dir);
    for (const auto& problem : result.problems) log("Games: "+problem);
    if (problems) *problems=result.problems;
    Games=std::move(result.games);
    log("Games: "+std::to_string(Games.size())+" loaded from "+std::filesystem::absolute(dir).string());
    return !Games.empty();
}
inline int findGame(const std::string& id) {
    for (size_t i=0;i<Games.size();++i) if (id==Games[i].id) return static_cast<int>(i);
    return -1;
}
// Which game owns a Steam app, or -1.
inline int findGameByApp(unsigned appId) {
    for (size_t i=0;i<Games.size();++i) if (Games[i].steamAppId==appId) return static_cast<int>(i);
    return -1;
}
// Every game has its own settings file next to the main config, so what suits one game never disturbs another.
inline std::filesystem::path profilePath(const std::filesystem::path& mainConfig,size_t game) {
    return mainConfig.parent_path()/Games[game].profile;
}
// Index of a known game that is running right now, or -1.
inline int runningGame() {
    for (size_t i=0;i<Games.size();++i) if (processRunning(Games[i].process)) return static_cast<int>(i);
    return -1;
}

// "path" entries of Steam's libraryfolders.vdf, with the VDF's doubled backslashes undone.
inline std::vector<std::string> parseLibraryPaths(const std::string& vdf) {
    std::vector<std::string> paths;
    static const std::regex entry("\"path\"\\s+\"([^\"]*)\"");
    for (std::sregex_iterator it(vdf.begin(),vdf.end(),entry),end;it!=end;++it) {
        std::string p=(*it)[1];
        std::string plain;
        for (size_t i=0;i<p.size();++i) { plain+=p[i]; if (p[i]=='\\' && i+1<p.size() && p[i+1]=='\\') ++i; }
        paths.push_back(plain);
    }
    return paths;
}
// Every Steam library folder on this PC. FLATTODEPTH_STEAM_LIBRARIES (folders separated by ;) overrides Steam's own
// record, for tests and unusual setups; the install scripts honour it too.
inline std::vector<std::filesystem::path> steamLibraries() {
    std::vector<std::filesystem::path> libraries;
    wchar_t forced[4096]{};
    if (GetEnvironmentVariableW(L"FLATTODEPTH_STEAM_LIBRARIES",forced,4096)>0) {
        std::wstringstream in(forced);
        for (std::wstring item;std::getline(in,item,L';');) if (!item.empty()) libraries.push_back(std::filesystem::path(item).lexically_normal());
        return libraries;
    }
    wchar_t root[MAX_PATH]{}; DWORD size=sizeof(root);
    if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,root,&size)!=ERROR_SUCCESS) return libraries;
    const std::filesystem::path steam=std::filesystem::path(root).lexically_normal();
    libraries.push_back(steam);
    std::ifstream vdf(steam/"steamapps"/"libraryfolders.vdf");
    std::stringstream text; text<<vdf.rdbuf();
    for (const auto& p : parseLibraryPaths(text.str())) {
        const auto lib=std::filesystem::path(p).lexically_normal();
        if (std::find(libraries.begin(),libraries.end(),lib)==libraries.end()) libraries.push_back(lib);
    }
    return libraries;
}

// One installed Steam game, as Steam's own record (steamapps\appmanifest_<id>.acf) describes it.
struct SteamApp {
    unsigned appId=0;
    std::string name,installDir;
    unsigned stateFlags=0;                 // 4 is "fully installed"; 0 means the record did not say
    std::filesystem::path library;
    std::filesystem::path directory() const { return library/"steamapps"/"common"/std::filesystem::path(installDir); }
    // A game being downloaded for the first time has a record but nothing to play yet.
    bool installed() const { return stateFlags==0 || (stateFlags&4)!=0; }
};
// Undoes the quoting of a VDF string value.
inline std::string vdfUnescape(const std::string& s) {
    std::string out;
    for (size_t i=0;i<s.size();++i) { if (s[i]=='\\' && i+1<s.size() && (s[i+1]=='\\' || s[i+1]=='"')) ++i; out+=s[i]; }
    return out;
}
inline bool parseAppManifest(const std::string& text,SteamApp& out) {
    auto value=[&](const char* key) {
        std::smatch m; const std::regex re(std::string("\"")+key+"\"\\s+\"((?:[^\"\\\\]|\\\\.)*)\"",std::regex::icase);
        return std::regex_search(text,m,re) ? vdfUnescape(m[1]) : std::string();
    };
    SteamApp app;
    try {
        const std::string id=value("appid");
        if (id.empty() || id.size()>10 || id.find_first_not_of("0123456789")!=std::string::npos) return false;
        const unsigned long long n=std::stoull(id);
        if (n==0 || n>4294967295ULL) return false;
        app.appId=static_cast<unsigned>(n);
        const std::string flags=value("StateFlags"); app.stateFlags=flags.empty() ? 0 : static_cast<unsigned>(std::stoul(flags));
    } catch (const std::exception&) { return false; }
    app.name=value("name"); app.installDir=value("installdir");
    if (app.installDir.empty() || app.installDir.find_first_of("\\/:")!=std::string::npos || app.installDir==".." || app.installDir==".") return false;   // never walk out of steamapps\common
    out=std::move(app);
    return true;
}
// Everything Steam says is installed, across all libraries. A game recorded in two libraries (a leftover) is reported
// once, from the library that comes first.
inline std::vector<SteamApp> scanSteamApps(const std::vector<std::filesystem::path>& libraries) {
    std::vector<SteamApp> apps; std::set<unsigned> seen;
    std::error_code ignored;
    for (const auto& library : libraries) {
        for (std::filesystem::directory_iterator it(library/"steamapps",ignored),end;!ignored && it!=end;it.increment(ignored)) {
            const std::string name=it->path().filename().string();
            if (name.rfind("appmanifest_",0)!=0 || it->path().extension()!=".acf") continue;
            std::ifstream f(it->path(),std::ios::binary);
            std::string head(16384,'\0'); f.read(head.data(),static_cast<std::streamsize>(head.size())); head.resize(static_cast<size_t>(f.gcount()));
            SteamApp app;
            if (!parseAppManifest(head,app) || !seen.insert(app.appId).second) continue;
            app.library=library; apps.push_back(std::move(app));
        }
    }
    std::sort(apps.begin(),apps.end(),[](const SteamApp& a,const SteamApp& b) { return a.appId<b.appId; });
    return apps;
}
inline const SteamApp* findSteamApp(const std::vector<SteamApp>& apps,unsigned appId) {
    for (const auto& a : apps) if (a.appId==appId) return &a;
    return nullptr;
}
// Asks Steam to start the game exactly as the library button would, launch options included.
inline bool launchSteamGame(unsigned appId) {
    // ShellExecute wants COM on the calling thread; a different apartment model already set up is fine.
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const auto url=L"steam://rungameid/"+std::to_wstring(appId);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
}

// A fresh install ships only flattodepth.default.ini. The first time a settings file is needed it is made from X.default.ini
// if there is one, else from flattodepth.default.ini, so the user's own copy is the one FlatToDepth saves window size and
// placement into, and updates never overwrite it.
inline void ensureSettings(const std::filesystem::path& file) {
    std::error_code ignored;
    if (std::filesystem::exists(file,ignored)) return;
    const auto dir=file.parent_path();
    auto defaults=file; defaults.replace_extension(); defaults+=".default.ini";   // flattodepth.ini -> flattodepth.default.ini
    if (!std::filesystem::exists(defaults,ignored)) defaults=dir/"flattodepth.default.ini";
    if (std::filesystem::exists(defaults,ignored)) std::filesystem::copy_file(defaults,file,ignored);
}
