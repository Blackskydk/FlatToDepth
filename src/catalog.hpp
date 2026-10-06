#pragma once
#include "common.hpp"
#include <cstring>
#include <map>
#include <regex>
#include <set>

// The catalog of games FlatToDepth can show in 3D. It is plain INI, one section per game, read from two files next to
// flattodepth.ini: games.catalog.ini (shipped, kept up to date by the project) and games.user.ini (yours, never overwritten;
// a section with the same name as a catalog one replaces it). scripts\games.ps1 reads the same files, so the menu and
// the installers can never disagree about a game. See docs\GAMES.md for the format.

// --- Plain INI --------------------------------------------------------------------------------------------------------
// [section], key=value. A line starting with ; or # is a comment. UTF-8, with or without a byte order mark. Sections and
// keys keep their file order; keys are case-insensitive (stored lower-case), the last of a repeated key wins.
struct IniSection {
    std::string name;
    std::vector<std::pair<std::string,std::string>> values;
    const std::string* find(const std::string& key) const {
        const std::string* found=nullptr;
        for (const auto& v : values) if (v.first==key) found=&v.second;
        return found;
    }
    std::string get(const std::string& key,const std::string& fallback={}) const { const auto* v=find(key); return v ? *v : fallback; }
};
inline std::string lowerCase(std::string s) {
    for (auto& c : s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline std::string trimmed(const std::string& s) {
    const char* space=" \t\r\n";
    const auto a=s.find_first_not_of(space);
    return a==std::string::npos ? std::string() : s.substr(a,s.find_last_not_of(space)-a+1);
}
inline std::vector<IniSection> parseIni(std::string text) {
    std::vector<IniSection> sections;
    if (text.rfind("\xEF\xBB\xBF",0)==0) text.erase(0,3);
    std::istringstream in(text);
    for (std::string raw;std::getline(in,raw);) {
        const std::string line=trimmed(raw);
        if (line.empty() || line[0]==';' || line[0]=='#') continue;
        if (line[0]=='[') {
            const auto end=line.find(']');
            if (end!=std::string::npos) sections.push_back({trimmed(line.substr(1,end-1)),{}});
            continue;
        }
        const auto eq=line.find('=');
        if (eq==std::string::npos || sections.empty()) continue;
        sections.back().values.push_back({lowerCase(trimmed(line.substr(0,eq))),trimmed(line.substr(eq+1))});
    }
    return sections;
}
inline std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,utf8.data(),static_cast<int>(utf8.size()),nullptr,0);
    std::wstring out(static_cast<size_t>(std::max(n,0)),L'\0');
    if (n>0) MultiByteToWideChar(CP_UTF8,0,utf8.data(),static_cast<int>(utf8.size()),out.data(),n);
    return out;
}
inline std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);
    std::stringstream s; s<<f.rdbuf();
    return s.str();
}

// --- Games ------------------------------------------------------------------------------------------------------------
// A Geo-11 fix shortcut the in-headset tools panel can press for you, since there is no keyboard in the headset.
struct GameKey {
    std::wstring label;
    WORD vk=0;                 // always one of F1..F12, see KeyPresser
};
constexpr size_t MaxGameKeys=6;
// One game FlatToDepth can bridge. Each has its own profile INI (eye order, crop, window size and placement), so what suits
// one game never disturbs another.
struct GameInfo {
    std::string id;            // --game value, and the section name in the catalog
    std::wstring title;        // shown in the in-headset menu
    std::wstring subtitle;
    unsigned steamAppId=0;
    std::wstring process;      // executable name while the game runs, e.g. oriDE.exe
    std::wstring profile;      // this game's settings INI, next to the main config
    std::vector<GameKey> keys; // the fix's shortcuts
    std::string machine;       // "x86", "x64" or "" (unknown): which executable the game has
    bool hasFix=false;         // the entry says where to download a stereo fix; otherwise you bring your own
    std::string source;        // "catalog" or "user"
};

// Checks a name that ends up in a file path or a command line: letters, digits and a few separators, never a folder.
inline bool plainName(const std::string& s,const char* allowed="_.-") {
    if (s.empty() || s.size()>96 || s.find("..")!=std::string::npos) return false;
    for (char c : s) if (!(std::isalnum(static_cast<unsigned char>(c)) || std::strchr(allowed,c))) return false;
    return true;
}
inline std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out; std::stringstream in(s);
    for (std::string item;std::getline(in,item,',');) { item=trimmed(item); if (!item.empty()) out.push_back(item); }
    return out;
}
// The file name a download address ends in, with %-escapes decoded and any ?query or #fragment dropped: what the archive
// is called on disk when the entry does not say.
inline std::string urlFileName(const std::string& url) {
    const auto scheme=url.find("://");
    std::string rest=scheme==std::string::npos ? url : url.substr(scheme+3);
    const auto slash=rest.find('/');
    rest=slash==std::string::npos ? std::string() : rest.substr(slash);
    rest=rest.substr(0,rest.find_first_of("?#"));
    std::string name=rest.substr(rest.rfind('/')==std::string::npos ? 0 : rest.rfind('/')+1),out;
    for (size_t i=0;i<name.size();++i) {
        if (name[i]=='%' && i+2<name.size() && std::isxdigit(static_cast<unsigned char>(name[i+1])) && std::isxdigit(static_cast<unsigned char>(name[i+2]))) { out+=static_cast<char>(std::stoi(name.substr(i+1,2),nullptr,16)); i+=2; }
        else out+=name[i];
    }
    return out;
}
// Turns one section into a game. Returns an empty string on success, otherwise what is wrong with it; a game with a
// problem is skipped, never half-loaded.
inline std::string gameFromSection(const IniSection& s,const std::string& source,GameInfo& out) {
    GameInfo g; g.id=s.name; g.source=source;
    if (g.id.empty() || g.id.size()>32 || !std::isalnum(static_cast<unsigned char>(g.id[0])) || !plainName(g.id,"_-") || g.id!=lowerCase(g.id))
        return "the section name must be 1 to 32 lower-case letters, digits, - or _";
    const std::string title=s.get("title");
    if (title.empty()) return "title is missing";
    g.title=widen(title); g.subtitle=widen(s.get("subtitle"));
    try {
        const std::string app=s.get("steam_app_id");
        if (app.empty() || app.size()>10 || app.find_first_not_of("0123456789")!=std::string::npos) throw std::invalid_argument("app");   // digits only: no sign, no spaces
        const unsigned long long id=std::stoull(app);
        if (id==0 || id>4294967295ULL) throw std::invalid_argument("app");
        g.steamAppId=static_cast<unsigned>(id);
    } catch (const std::exception&) { return "steam_app_id must be the game's Steam app number"; }
    const std::string exe=s.get("exe");
    if (!plainName(exe," _.-+") || exe.size()<5 || _stricmp(exe.c_str()+exe.size()-4,".exe")!=0) return "exe must be the game's executable name, like game.exe, with no folder";
    g.process=widen(exe);
    g.machine=s.get("machine");
    if (g.machine!="" && g.machine!="x86" && g.machine!="x64") return "machine must be x86 or x64";
    const std::string profile=s.get("profile","flattodepth-"+g.id+".ini");
    if (!plainName(profile) || profile.size()<5 || _stricmp(profile.c_str()+profile.size()-4,".ini")!=0) return "profile must be a file name ending in .ini, with no folder";
    g.profile=widen(profile);
    for (size_t k=1;k<=MaxGameKeys;++k) {
        const std::string* spec=s.find("key"+std::to_string(k));
        if (!spec) continue;
        const auto bar=spec->rfind('|');
        const std::string label=bar==std::string::npos ? std::string() : trimmed(spec->substr(0,bar));
        const std::string key=bar==std::string::npos ? std::string() : trimmed(spec->substr(bar+1));
        if (label.empty() || label.size()>24 || !std::regex_match(key,std::regex("[Ff]([1-9]|1[0-2])")))
            return "key"+std::to_string(k)+" must look like  Label|F1  with a key from F1 to F12";
        const WORD vk=static_cast<WORD>(VK_F1+std::stoi(key.substr(1))-1);
        for (const auto& existing : g.keys) if (existing.vk==vk) return "key"+std::to_string(k)+" repeats a key that is already used";
        g.keys.push_back({widen(label),vk});
    }
    for (const auto& dll : splitList(s.get("shim_files")))
        if (!std::regex_match(dll,std::regex("xinput[A-Za-z0-9_.]*\\.dll",std::regex::icase))) return "shim_files may only name xinput*.dll files";
    for (const char* name : {"shim_manifest","fix_manifest","fix_dir"}) { const auto v=s.get(name); if (!v.empty() && !plainName(v)) return std::string(name)+" must be a plain file or folder name"; }
    const std::string url=s.get("fix_url");
    if (!url.empty()) {
        if (url.rfind("https://",0)!=0 || url.find_first_of(" \t\"'<>`")!=std::string::npos) return "fix_url must be an https:// address";
        if (!std::regex_match(s.get("fix_sha256"),std::regex("[0-9A-Fa-f]{64}"))) return "fix_url needs fix_sha256, the SHA256 of the download (64 hex digits), so only the inspected file is ever installed";
        const std::string archive=s.get("fix_archive",urlFileName(url));
        if (!plainName(archive,"_.-+")) return "fix_archive must be a plain file name (the address does not end in one, so name it)";
        if (!s.get("fix_inner").empty() && !plainName(s.get("fix_inner"),"_.-+")) return "fix_inner must be a plain file name";
        if (!s.get("fix_marker").empty() && !plainName(s.get("fix_marker"),"_.-+")) return "fix_marker must be a plain file name";
        g.hasFix=true;
    }
    out=std::move(g);
    return {};
}

struct CatalogResult {
    std::vector<GameInfo> games;
    std::vector<std::string> problems;     // one line each, ready for the log
};
// Merges the shipped catalog and the user's file. A user section with a catalog section's name replaces it in place.
inline CatalogResult loadCatalogText(const std::string& catalogText,const std::string& userText) {
    CatalogResult r;
    auto add=[&](const std::string& text,const std::string& source) {
        std::set<std::string> seen;
        for (const auto& section : parseIni(text)) {
            GameInfo g; const std::string problem=gameFromSection(section,source,g);
            if (!problem.empty()) { r.problems.push_back(source+" games file: ["+section.name+"] skipped: "+problem); continue; }
            if (!seen.insert(g.id).second) { r.problems.push_back(source+" games file: ["+g.id+"] appears twice; the later one is used"); }
            const auto same=std::find_if(r.games.begin(),r.games.end(),[&](const GameInfo& o) { return o.id==g.id; });
            if (same!=r.games.end()) *same=std::move(g); else r.games.push_back(std::move(g));
        }
    };
    add(catalogText,"catalog"); add(userText,"user");
    // Two games with the same Steam app or the same executable cannot both be recognised: say so, keep both.
    for (size_t i=0;i<r.games.size();++i) for (size_t j=i+1;j<r.games.size();++j) {
        if (r.games[i].steamAppId==r.games[j].steamAppId) r.problems.push_back("games ["+r.games[i].id+"] and ["+r.games[j].id+"] have the same steam_app_id");
        else if (_wcsicmp(r.games[i].process.c_str(),r.games[j].process.c_str())==0) r.problems.push_back("games ["+r.games[i].id+"] and ["+r.games[j].id+"] have the same exe; the first one running is taken");
    }
    return r;
}
inline CatalogResult loadCatalogFiles(const std::filesystem::path& dir) {
    auto r=loadCatalogText(readTextFile(dir/"games.catalog.ini"),readTextFile(dir/"games.user.ini"));
    if (!std::filesystem::exists(dir/"games.catalog.ini")) r.problems.insert(r.problems.begin(),"games.catalog.ini was not found in "+dir.string()+"; only your games.user.ini entries are available");
    return r;
}
