#pragma once
#include "games.hpp"
#include <functional>

// `FlatToDepth.exe --scan`: looks at every game Steam has installed and says which of them FlatToDepth can show in 3D (they are in
// the catalog), which might be worth trying (they draw with DirectX 11, which is what the Geo-11 stereo fixes work on,
// but nobody has made them an entry yet) and which cannot work with this pipeline. It reads the games' executables
// without running them. It cannot know that a stereo fix exists for a game: that is what a catalog entry records.

// --- Reading executables ----------------------------------------------------------------------------------------------
struct PeInfo {
    bool valid=false,x64=false;
    std::set<std::string> imports;      // DLL names the executable imports (also delay-loaded), lower-case
};
// Reads a Windows executable's bitness and its import tables, touching only the headers.
inline PeInfo readPe(const std::filesystem::path& file) {
    PeInfo info;
    std::ifstream f(file,std::ios::binary);
    if (!f) return info;
    auto read=[&](uint64_t offset,void* out,size_t n) {
        f.clear(); f.seekg(static_cast<std::streamoff>(offset),std::ios::beg); f.read(static_cast<char*>(out),static_cast<std::streamsize>(n));
        return static_cast<size_t>(f.gcount())==n;
    };
    auto u16=[&](uint64_t off,uint16_t& v) { return read(off,&v,2); };
    auto u32=[&](uint64_t off,uint32_t& v) { return read(off,&v,4); };
    uint16_t mz=0; uint32_t pe=0,sig=0;
    if (!u16(0,mz) || mz!=0x5A4D || !u32(0x3c,pe) || pe>(1u<<20) || !u32(pe,sig) || sig!=0x00004550) return info;
    uint16_t machine=0,sections=0,optionalSize=0,magic=0;
    if (!u16(pe+4,machine) || !u16(pe+6,sections) || !u16(pe+20,optionalSize) || !u16(pe+24,magic)) return info;
    if (magic!=0x10b && magic!=0x20b) return info;
    info.x64=magic==0x20b;
    info.valid=true;
    // Data directories: 96 bytes into a PE32 optional header, 112 into a PE32+ one.
    const uint64_t optional=pe+24,directories=optional+(info.x64 ? 112 : 96);
    struct Section { uint32_t virtualSize,virtualAddress,rawSize,rawPointer; };
    std::vector<Section> table;
    if (sections==0 || sections>96) return info;
    for (uint16_t i=0;i<sections;++i) {
        const uint64_t at=optional+optionalSize+static_cast<uint64_t>(i)*40;
        Section s{}; if (!u32(at+8,s.virtualSize) || !u32(at+12,s.virtualAddress) || !u32(at+16,s.rawSize) || !u32(at+20,s.rawPointer)) return info;
        table.push_back(s);
    }
    auto toOffset=[&](uint32_t rva,uint64_t& out) {
        for (const auto& s : table) if (rva>=s.virtualAddress && rva<s.virtualAddress+std::max(s.virtualSize,s.rawSize)) { out=static_cast<uint64_t>(rva-s.virtualAddress)+s.rawPointer; return true; }
        return false;
    };
    auto dllName=[&](uint32_t rva) {
        uint64_t at=0; std::string name;
        if (!toOffset(rva,at)) return name;
        char buffer[260]{}; f.clear(); f.seekg(static_cast<std::streamoff>(at),std::ios::beg); f.read(buffer,sizeof(buffer)-1);
        name.assign(buffer,strnlen(buffer,sizeof(buffer)-1));
        return lowerCase(name);
    };
    // Import directory (index 1): 20-byte descriptors, the DLL name at +12. Delay imports (index 13): 32 bytes, name at +4.
    for (const auto& dir : {std::make_pair(1,20u),std::make_pair(13,32u)}) {
        uint32_t rva=0,size=0; uint64_t at=0;
        if (!u32(directories+static_cast<uint64_t>(dir.first)*8,rva) || !u32(directories+static_cast<uint64_t>(dir.first)*8+4,size) || rva==0 || !toOffset(rva,at)) continue;
        for (uint32_t i=0;i<1024;++i) {
            uint32_t name=0;
            if (!u32(at+static_cast<uint64_t>(i)*dir.second+(dir.first==1 ? 12 : 4),name) || name==0) break;
            const std::string dll=dllName(name);
            if (!dll.empty()) info.imports.insert(dll);
        }
    }
    return info;
}
// Does the file contain this text (ASCII, case-insensitive)? Games often load d3d11.dll and the XInput DLL by name while
// running rather than importing them, so the name is all there is to find. Reads at most maxBytes.
inline bool fileMentions(const std::filesystem::path& file,const std::string& needle,uint64_t maxBytes=192ull<<20) {
    std::ifstream f(file,std::ios::binary);
    if (!f || needle.empty()) return false;
    const std::string want=lowerCase(needle);
    std::vector<char> buffer(1u<<20);
    std::string carry; uint64_t total=0;
    while (f && total<maxBytes) {
        f.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
        const auto n=static_cast<size_t>(f.gcount()); if (n==0) break;
        total+=n;
        std::string chunk=carry; chunk.append(buffer.data(),n);
        for (auto& c : chunk) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (chunk.find(want)!=std::string::npos) return true;
        carry=chunk.substr(chunk.size()>want.size() ? chunk.size()-want.size()+1 : 0);
    }
    return false;
}

// --- What a game is made of -------------------------------------------------------------------------------------------
enum class Verdict { Supported, Candidate, Unlikely, Unknown };
struct GameAnalysis {
    SteamApp app;
    Verdict verdict=Verdict::Unknown;
    int catalogGame=-1;                       // index into Games when supported
    std::filesystem::path exe;                // the executable that was examined, if any
    bool x64=false;
    std::set<std::string> graphics;           // "DirectX 9", "DirectX 11", ...
    std::set<std::string> xinput;             // "xinput1_3", ...
    bool dx11=false;
    std::string note;                         // why, in a sentence
};
// The executable most likely to be the game: the catalog's, if it names one, else the biggest one within three folders,
// leaving out installers, crash reporters and redistributables.
inline std::filesystem::path findGameExe(const std::filesystem::path& dir,const std::wstring& preferred={}) {
    std::error_code ec;
    if (!preferred.empty() && std::filesystem::exists(dir/preferred,ec)) return dir/preferred;
    static const std::regex skip("(unins|uninst|crash|report|redist|vcredist|vc_redist|dxsetup|dotnet|setup|install|update|helper|bootstrap|notification|cefprocess|webhelper|eac|battleye)",std::regex::icase);
    std::filesystem::path best; uintmax_t bestSize=0;
    std::vector<std::pair<std::filesystem::path,int>> todo{{dir,0}};
    size_t visited=0;
    while (!todo.empty() && visited<4000) {
        const auto [folder,depth]=todo.back(); todo.pop_back();
        for (std::filesystem::directory_iterator it(folder,ec),end;!ec && it!=end;it.increment(ec)) {
            ++visited;
            std::error_code e2; const auto& path=it->path();
            if (it->is_directory(e2)) { if (depth<3) todo.push_back({path,depth+1}); continue; }
            if (_stricmp(path.extension().string().c_str(),".exe")!=0 || std::regex_search(path.filename().string(),skip)) continue;
            const auto size=it->file_size(e2);
            if (!e2 && size>bestSize) { best=path; bestSize=size; }
        }
    }
    return best;
}
inline GameAnalysis analyseGame(const SteamApp& app) {
    GameAnalysis a; a.app=app;
    const int known=findGameByApp(app.appId);
    const std::filesystem::path dir=app.directory();
    a.exe=findGameExe(dir,known>=0 ? Games[static_cast<size_t>(known)].process : std::wstring());
    std::error_code ec;
    if (a.exe.empty() || !std::filesystem::exists(a.exe,ec)) { a.note="no executable found"; a.catalogGame=known; a.verdict=known>=0 ? Verdict::Supported : Verdict::Unknown; return a; }
    const PeInfo exe=readPe(a.exe);
    a.x64=exe.x64;
    std::set<std::string> imports=exe.imports;
    // The engine's own DLLs next to the executable (Unity keeps its renderer in UnityPlayer.dll) draw and read input too.
    const auto folder=a.exe.parent_path(); size_t dlls=0;
    for (std::filesystem::directory_iterator it(folder,ec),end;!ec && it!=end && dlls<80;it.increment(ec)) {
        if (_stricmp(it->path().extension().string().c_str(),".dll")!=0) continue;
        ++dlls; const PeInfo dll=readPe(it->path());
        imports.insert(dll.imports.begin(),dll.imports.end());
    }
    auto has=[&](const char* name) { return imports.count(name)>0; };
    std::vector<std::filesystem::path> textual{a.exe};
    if (std::filesystem::exists(folder/"UnityPlayer.dll",ec)) textual.push_back(folder/"UnityPlayer.dll");
    auto mentions=[&](const char* name) { for (const auto& p : textual) if (fileMentions(p,name)) return true; return false; };
    if (has("d3d9.dll")) a.graphics.insert("DirectX 9");
    if (has("d3d10.dll") || has("d3d10_1.dll")) a.graphics.insert("DirectX 10");
    if (has("d3d12.dll")) a.graphics.insert("DirectX 12");
    if (has("opengl32.dll")) a.graphics.insert("OpenGL");
    if (has("vulkan-1.dll")) a.graphics.insert("Vulkan");
    a.dx11=has("d3d11.dll") || mentions("d3d11.dll");
    if (a.dx11) a.graphics.insert("DirectX 11");
    for (const char* name : {"xinput1_3","xinput1_4","xinput9_1_0"})
        if (has((std::string(name)+".dll").c_str()) || mentions((std::string(name)+".dll").c_str())) a.xinput.insert(name);
    a.catalogGame=known;
    if (known>=0) { a.verdict=Verdict::Supported; a.note="in the catalog"; }
    else if (a.dx11) { a.verdict=Verdict::Candidate; a.note="draws with DirectX 11, so a Geo-11 fix could work, if one exists for it"; }
    else if (!exe.valid) { a.verdict=Verdict::Unknown; a.note="not a Windows program FlatToDepth can read"; }
    else { a.verdict=Verdict::Unlikely; a.note=a.graphics.empty() ? "no DirectX 11 found" : "does not use DirectX 11"; }
    return a;
}
// Everything installed, examined, supported first, then candidates by name.
inline std::vector<GameAnalysis> scanInstalledGames(const std::vector<SteamApp>& apps,const std::function<void(const SteamApp&)>& progress={}) {
    std::vector<GameAnalysis> out;
    for (const auto& app : apps) {
        if (!app.installed()) continue;
        if (progress) progress(app);
        out.push_back(analyseGame(app));
    }
    std::stable_sort(out.begin(),out.end(),[](const GameAnalysis& a,const GameAnalysis& b) {
        if (a.verdict!=b.verdict) return static_cast<int>(a.verdict)<static_cast<int>(b.verdict);
        return lowerCase(a.app.name)<lowerCase(b.app.name);
    });
    return out;
}

// --- Reporting --------------------------------------------------------------------------------------------------------
inline const char* verdictName(Verdict v) {
    switch (v) { case Verdict::Supported: return "supported"; case Verdict::Candidate: return "candidate"; case Verdict::Unlikely: return "unlikely"; default: return "unknown"; }
}
inline std::string joinSet(const std::set<std::string>& s,const char* separator=", ") {
    std::string out; for (const auto& i : s) out+=(out.empty() ? "" : separator)+i; return out;
}
// A section name for a draft entry: the game's name as lower-case letters, digits and dashes.
inline std::string slugFor(const std::string& name,unsigned appId) {
    std::string slug; bool dash=false;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) { slug+=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); dash=false; }
        else if (!slug.empty() && !dash) { slug+='-'; dash=true; }
    }
    while (!slug.empty() && slug.back()=='-') slug.pop_back();
    if (slug.size()>28) slug.resize(28);
    while (!slug.empty() && slug.back()=='-') slug.pop_back();
    return slug.empty() ? "game-"+std::to_string(appId) : slug;
}
// A starting point for games.user.ini. The stereo fix itself is the part only a person can supply.
inline std::string draftEntry(const GameAnalysis& a) {
    std::ostringstream o;
    o<<"[" << slugFor(a.app.name,a.app.appId) << "]\n"
     <<"title=" << a.app.name << "\n"
     <<"steam_app_id=" << a.app.appId << "\n";
    if (!a.exe.empty()) o<<"exe=" << a.exe.filename().string() << "\n";
    o<<"folder=" << a.app.installDir << "\n";
    if (!a.exe.empty()) o<<"machine=" << (a.x64 ? "x64" : "x86") << "\n";
    if (a.x64) o<<"shim_files=xinput1_4.dll,xinput1_3.dll\n";
    else if (a.xinput.count("xinput9_1_0") || a.xinput.empty()) o<<"shim_files=xinput9_1_0.dll\n";
    else o<<"; The 32-bit controller shim only exists as xinput9_1_0.dll; this game loads " << joinSet(a.xinput) << ", so its controller will not work yet.\n";
    o<<"; Shortcuts for the tools panel, if the fix has any:  key1=Convergence|F1\n"
     <<"; To install a stereo fix for it, find one and add fix_url, fix_sha256 and fix_archive (see docs/GAMES.md).\n"
     <<"; Without them, install the fix yourself; FlatToDepth still handles the menu, the controller and the settings.\n";
    return o.str();
}
inline std::string scanReport(const std::vector<GameAnalysis>& results,bool all) {
    std::ostringstream o;
    size_t supported=0,candidates=0,others=0;
    for (const auto& r : results) { if (r.verdict==Verdict::Supported) ++supported; else if (r.verdict==Verdict::Candidate) ++candidates; else ++others; }
    o<<"FlatToDepth game scan: "<<results.size()<<" installed Steam games: "<<supported<<" supported, "<<candidates<<" candidate"<<(candidates==1 ? "" : "s")<<", "<<others<<" other\n\n";
    // Fixed-width columns; a name too long for its column is cut short, never run into the next one.
    auto pad=[](std::string s,size_t n) { if (s.size()>=n) s.resize(n-1); return s+std::string(n-s.size(),' '); };
    o<<pad("STATUS",11)<<pad("APP",9)<<pad("GAME",40)<<pad("BITS",6)<<pad("GRAPHICS",18)<<"CONTROLLER\n";
    for (const auto& r : results) {
        if (!all && r.verdict!=Verdict::Supported && r.verdict!=Verdict::Candidate) continue;
        o<<pad(verdictName(r.verdict),11)<<pad(std::to_string(r.app.appId),9)<<pad(r.app.name,40)<<pad(r.exe.empty() ? "?" : (r.x64 ? "x64" : "x86"),6)
         <<pad(r.graphics.empty() ? "-" : joinSet(r.graphics,"/"),18)<<(r.xinput.empty() ? "-" : joinSet(r.xinput,"/"))<<"\n";
    }
    if (!all && others) o<<"\n("<<others<<" other games hidden; run with --all to list them)\n";
    if (candidates) o<<"\nCandidates draw with DirectX 11, which the Geo-11 stereo fixes work on, but a game also needs a fix made for it.\n"
        <<"For one you want to try: FlatToDepth.exe --scan --draft <app number> prints a games.user.ini entry to start from.\n";
    return o.str();
}
