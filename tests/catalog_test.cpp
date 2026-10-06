#include "common.hpp"
#include "scan.hpp"
#include <algorithm>
#include <cstdio>

// The games catalog, the Steam scan, and the scan report: everything that decides which games FlatToDepth offers.
static int failures=0;
#define CHECK(cond,msg) do { if (!(cond)) { ++failures; std::cerr<<"FAIL line "<<__LINE__<<": "<<(msg)<<'\n'; } } while (0)

namespace fs=std::filesystem;
static void writeFile(const fs::path& p,const std::string& text) { fs::create_directories(p.parent_path()); std::ofstream(p,std::ios::binary)<<text; }
static std::string slurp(const fs::path& p) { return readTextFile(p); }
static bool contains(const std::string& s,const std::string& part) { return s.find(part)!=std::string::npos; }
static bool anyProblem(const CatalogResult& r,const std::string& part) { for (const auto& p : r.problems) if (contains(p,part)) return true; return false; }

// A Windows executable with just enough structure for readPe: one section holding an import table and, if asked, a
// delay-import table. `payload` goes after the headers (to hide text in, as games load DLLs by name).
static void buildPe(const fs::path& path,bool x64,const std::vector<std::string>& imports,const std::vector<std::string>& delayed={},const std::string& payload={},size_t padding=0) {
    std::vector<uint8_t> f(0x400,0);
    auto put16=[&](size_t at,uint16_t v) { f[at]=static_cast<uint8_t>(v); f[at+1]=static_cast<uint8_t>(v>>8); };
    auto put32=[&](size_t at,uint32_t v) { for (int i=0;i<4;++i) f[at+static_cast<size_t>(i)]=static_cast<uint8_t>(v>>(8*i)); };
    put16(0,0x5A4D); put32(0x3c,0x80);
    put32(0x80,0x00004550);
    put16(0x84,x64 ? 0x8664 : 0x14c); put16(0x86,1);
    const uint16_t optionalSize=x64 ? 240 : 224;
    put16(0x84+16,optionalSize); put16(0x98,x64 ? 0x20b : 0x10b);
    const size_t optional=0x98,directories=optional+(x64 ? 112 : 96),sectionTable=optional+optionalSize;
    // Section data: import descriptors (20 bytes each, plus a terminator), delay descriptors (32 bytes each), then names.
    const uint32_t va=0x1000;
    const size_t importBytes=(imports.size()+1)*20,delayAt=(importBytes+15)&~size_t(15),delayBytes=(delayed.size()+1)*32;
    size_t namesAt=delayAt+delayBytes;
    std::vector<uint8_t> data(namesAt,0);
    auto dput32=[&](size_t at,uint32_t v) { for (int i=0;i<4;++i) data[at+static_cast<size_t>(i)]=static_cast<uint8_t>(v>>(8*i)); };
    auto addName=[&](const std::string& n) { const uint32_t rva=va+static_cast<uint32_t>(data.size()); data.insert(data.end(),n.begin(),n.end()); data.push_back(0); return rva; };
    for (size_t i=0;i<imports.size();++i) dput32(i*20+12,addName(imports[i]));
    for (size_t i=0;i<delayed.size();++i) { dput32(delayAt+i*32,1); dput32(delayAt+i*32+4,addName(delayed[i])); }
    put32(directories+1*8,imports.empty() ? 0 : va); put32(directories+1*8+4,static_cast<uint32_t>(importBytes));
    put32(directories+13*8,delayed.empty() ? 0 : va+static_cast<uint32_t>(delayAt)); put32(directories+13*8+4,static_cast<uint32_t>(delayBytes));
    // Section header.
    std::memcpy(&f[sectionTable],".idata",6);
    put32(sectionTable+8,static_cast<uint32_t>(data.size())); put32(sectionTable+12,va);
    put32(sectionTable+16,static_cast<uint32_t>(data.size())); put32(sectionTable+20,0x400);
    f.insert(f.end(),data.begin(),data.end());
    f.insert(f.end(),padding,0);
    f.insert(f.end(),payload.begin(),payload.end());
    fs::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(f.data()),static_cast<std::streamsize>(f.size()));
}
static std::string manifest(unsigned app,const std::string& name,const std::string& dir,unsigned flags=4) {
    return "\"AppState\"\n{\n\t\"appid\"\t\t\""+std::to_string(app)+"\"\n\t\"Universe\"\t\t\"1\"\n\t\"name\"\t\t\""+name+"\"\n\t\"StateFlags\"\t\t\""+std::to_string(flags)+
        "\"\n\t\"installdir\"\t\t\""+dir+"\"\n\t\"UserConfig\"\n\t{\n\t\t\"name\"\t\t\"ignored\"\n\t}\n}\n";
}

int main(int argc,char** argv) {
    const fs::path root=argc>=2 ? fs::path(argv[1]) : fs::current_path();
    const fs::path temp=fs::temp_directory_path()/("flattodepth-catalog-test-"+std::to_string(GetCurrentProcessId()));
    std::error_code ignored; fs::remove_all(temp,ignored); fs::create_directories(temp);

    // --- Plain INI -------------------------------------------------------------------------------------------------
    {
        const auto s=parseIni("\xEF\xBB\xBF; comment\r\n# another\r\nstray=1\r\n[ First ]\r\nKey = Value with spaces \r\nkey=last wins\r\n\r\nUrl=https://x.example/a?b=c\r\n[second]\r\nempty=\r\nnoequals\r\n");
        CHECK(s.size()==2 && s[0].name=="First" && s[1].name=="second","sections are read in order, and names are trimmed; keys before any section are ignored");
        CHECK(s[0].get("key")=="last wins" && s[0].values.size()==3,"keys in a file are case-insensitive (Key is found as key) and the last of a repeated key wins");
        CHECK(s[0].get("url")=="https://x.example/a?b=c","only the first = splits a line");
        CHECK(s[1].find("empty") && s[1].get("empty","fallback")=="" && !s[1].find("noequals"),"empty values are kept, lines without = are skipped");
        CHECK(s[0].get("missing","d")=="d" && !s[0].find("missing"),"missing keys give the fallback");
        CHECK(parseIni("").empty() && parseIni("key=value\n").empty() && parseIni("[unterminated\nkey=1\n").empty(),"nothing to read: no sections");
        CHECK(widen("Ori \xC5\x8Dri")==L"Ori \u014Dri" && widen("").empty(),"UTF-8 becomes UTF-16");
        CHECK(trimmed("  \t x y \r\n")=="x y" && trimmed("   ").empty(),"trimming");
        CHECK(urlFileName("https://example.org/a/b/Fix_1.7z")=="Fix_1.7z" && urlFileName("https://example.org/Fix.7z?x=1#y")=="Fix.7z" && urlFileName("https://example.org/a%20b.7z")=="a b.7z" &&
            urlFileName("https://example.org/a%2Fb.7z")=="a/b.7z" && urlFileName("https://example.org/dir/").empty() && urlFileName("https://example.org").empty() && urlFileName("https://example.org/a%zz.7z")=="a%zz.7z" && urlFileName("https://example.org/ends%")=="ends%","file names from download addresses");
    }

    // --- One game from a section ----------------------------------------------------------------------------------
    auto load=[](const std::string& text,GameInfo& g) { const auto s=parseIni(text); return gameFromSection(s.at(0),"test",g); };
    const std::string good="[hollow-knight]\ntitle=Hollow Knight\nsteam_app_id=367520\nexe=hollow_knight.exe\n";
    {
        GameInfo g;
        CHECK(load(good,g).empty() && g.id=="hollow-knight" && g.title==L"Hollow Knight" && g.steamAppId==367520 && g.process==L"hollow_knight.exe",
            "the smallest valid entry: title, Steam app and executable");
        CHECK(g.profile==L"flattodepth-hollow-knight.ini" && g.keys.empty() && !g.hasFix && g.machine.empty() && g.subtitle.empty() && g.source=="test","everything else has a sensible default");
        CHECK(load(good+"subtitle=Silksong\nmachine=x64\nprofile=hk.ini\nkey1=Convergence|F1\nkey2 = HUD | f12\nshim_files=xinput1_4.dll, XInput1_3.dll\n",g).empty() &&
            g.subtitle==L"Silksong" && g.machine=="x64" && g.profile==L"hk.ini" && g.keys.size()==2 && g.keys[0].label==L"Convergence" && g.keys[0].vk==VK_F1 && g.keys[1].label==L"HUD" && g.keys[1].vk==VK_F12,"optional fields");
        const std::string sha(64,'a');
        CHECK(load(good+"fix_url=https://example.org/fix.7z\nfix_sha256="+sha+"\nfix_archive=fix.7z\nfix_inner=Inner.7z\nfix_marker=d3dx.ini\nfix_dir=hk-fix\nfix_manifest=geo11-hk.json\nshim_manifest=gamepad-hk.json\n",g).empty() && g.hasFix,"an entry that says where its fix is");
    }
    // The cases shared with tests/catalog_test.ps1: what is valid and what is refused, and why.
    {
        std::ifstream in(root/"tests"/"catalog_cases.txt");
        CHECK(static_cast<bool>(in),"tests/catalog_cases.txt is missing");
        auto expand=[](std::string t) {
            auto all=[&](const std::string& from,const std::string& to) { for (size_t at=0;(at=t.find(from,at))!=std::string::npos;at+=to.size()) t.replace(at,from.size(),to); };
            all("<NL>","\n"); all("{sha}",std::string(64,'a')); all("{SHA}",std::string(64,'A')); all("{z64}",std::string(64,'z'));
            all("{a32}",std::string(32,'a')); all("{a33}",std::string(33,'a')); all("{x24}",std::string(24,'x')); all("{x25}",std::string(25,'x'));
            return t;
        };
        size_t good=0,bad=0;
        for (std::string line;std::getline(in,line);) {
            if (!line.empty() && line.back()=='\r') line.pop_back();
            if (line.empty() || line[0]=='#') continue;
            std::vector<std::string> f; std::stringstream ss(line); for (std::string part;std::getline(ss,part,'\t');) f.push_back(part);
            if (line.back()=='\t') f.push_back("");
            CHECK(f.size()==4,"a case has four tab-separated fields: "+line.substr(0,40));
            if (f.size()!=4) continue;
            GameInfo g; const auto sections=parseIni(expand(f[3]));
            const std::string why=sections.empty() ? std::string("no section") : gameFromSection(sections[0],"test",g);
            if (f[0]=="good") { ++good; CHECK(why.empty(),"accepted: "+f[1]+" (got: "+why+")"); }
            else { ++bad; CHECK(!why.empty() && contains(why,f[2]),"refused: "+f[1]+" because "+f[2]+" (got: "+why+")"); }
        }
        CHECK(good>=10 && bad>=50,"the shared cases were read ("+std::to_string(good)+" good, "+std::to_string(bad)+" bad)");
    }

    // --- Merging the shipped catalog and the user's file -----------------------------------------------------------
    {
        const std::string catalog="[one]\ntitle=One\nsteam_app_id=1\nexe=one.exe\n[two]\ntitle=Two\nsteam_app_id=2\nexe=two.exe\n[three]\ntitle=Three\nsteam_app_id=3\nexe=three.exe\n";
        const std::string user="[two]\ntitle=Two, mine\nsteam_app_id=2\nexe=two2.exe\n[extra]\ntitle=Extra\nsteam_app_id=9\nexe=extra.exe\n[broken]\ntitle=No app\nexe=b.exe\n";
        const auto r=loadCatalogText(catalog,user);
        CHECK(r.games.size()==4 && r.games[0].id=="one" && r.games[1].id=="two" && r.games[2].id=="three" && r.games[3].id=="extra","a user section replaces the catalog's in place, new ones go last");
        CHECK(r.games[1].title==L"Two, mine" && r.games[1].source=="user" && r.games[0].source=="catalog","and it says where each came from");
        CHECK(anyProblem(r,"[broken] skipped") && r.problems.size()==1,"a broken user entry is skipped and reported, nothing else is");
        CHECK(loadCatalogText("","").games.empty() && loadCatalogText(catalog,"").games.size()==3,"either file may be empty");
        const auto twice=loadCatalogText("[a]\ntitle=A\nsteam_app_id=1\nexe=a.exe\n[a]\ntitle=A again\nsteam_app_id=1\nexe=a.exe\n","");
        CHECK(twice.games.size()==1 && twice.games[0].title==L"A again" && anyProblem(twice,"appears twice"),"a repeated section is reported and the later one is used");
        const auto sameApp=loadCatalogText("[a]\ntitle=A\nsteam_app_id=1\nexe=a.exe\n[b]\ntitle=B\nsteam_app_id=1\nexe=b.exe\n","");
        CHECK(sameApp.games.size()==2 && anyProblem(sameApp,"same steam_app_id"),"two games for one Steam app are both kept, with a warning");
        const auto sameExe=loadCatalogText("[a]\ntitle=A\nsteam_app_id=1\nexe=A.exe\n[b]\ntitle=B\nsteam_app_id=2\nexe=a.EXE\n","");
        CHECK(sameExe.games.size()==2 && anyProblem(sameExe,"same exe"),"the same executable twice is warned about, whatever the case");
        // From files.
        const auto dir=temp/"catalogs"; writeFile(dir/"games.catalog.ini",catalog); writeFile(dir/"games.user.ini",user);
        const auto files=loadCatalogFiles(dir);
        CHECK(files.games.size()==4,"read from the two files next to the config");
        const auto missing=loadCatalogFiles(temp/"nothing-here");
        CHECK(missing.games.empty() && anyProblem(missing,"games.catalog.ini was not found"),"no catalog file is reported");
        writeFile(temp/"only-user"/"games.user.ini",user); CHECK(loadCatalogFiles(temp/"only-user").games.size()==2 && anyProblem(loadCatalogFiles(temp/"only-user"),"not found"),"a user file alone still works");
    }

    // --- The catalog FlatToDepth ships -----------------------------------------------------------------------------------
    {
        const auto r=loadCatalogFiles(root);
        for (const auto& p : r.problems) std::cerr<<"catalog problem: "<<p<<'\n';
        CHECK(r.problems.empty(),"the shipped catalog has no problems");
        CHECK(r.games.size()>=2 && r.games[0].id=="blindforest" && r.games[1].id=="wotw","the Ori games come first");
        const auto& bf=r.games[0]; const auto& wotw=r.games[1];
        CHECK(bf.steamAppId==387290 && bf.process==L"oriDE.exe" && bf.profile==L"flattodepth-blindforest.ini" && bf.machine=="x86" && bf.hasFix && bf.title==L"Ori and the Blind Forest" && bf.subtitle==L"Definitive Edition","Blind Forest");
        CHECK(wotw.steamAppId==1057090 && wotw.process==L"oriwotw.exe" && wotw.profile==L"flattodepth-wotw.ini" && wotw.machine=="x64" && wotw.hasFix,"Will of the Wisps");
        std::set<unsigned> apps; std::set<std::string> ids; std::set<std::wstring> procs,profiles;
        for (const auto& g : r.games) { apps.insert(g.steamAppId); ids.insert(g.id); procs.insert(g.process); profiles.insert(g.profile); }
        CHECK(apps.size()==r.games.size() && ids.size()==r.games.size() && procs.size()==r.games.size(),"ids, Steam apps and executables are unique");
        CHECK(profiles.size()==r.games.size() && !profiles.count(L"flattodepth.ini"),"every game has its own settings file, and none uses the main one");
        // Every pinned fix is a real hash and an https address (parsed again here, straight from the file, since GameInfo keeps only a flag).
        for (const auto& s : parseIni(slurp(root/"games.catalog.ini"))) {
            if (s.get("fix_url").empty()) continue;
            CHECK(std::regex_match(s.get("fix_sha256"),std::regex("[0-9A-Fa-f]{64}")) && s.get("fix_url").rfind("https://",0)==0,"["+s.name+"] pins its download with a SHA256");
        }
        // The two games' shortcuts, as documented in USAGE.md.
        CHECK(bf.keys.size()==5 && bf.keys[0].vk==VK_F1 && bf.keys[4].label==L"Bloom" && wotw.keys.size()==6 && wotw.keys[3].label==L"Blurriness" && wotw.keys[5].vk==VK_F6,"the Geo-11 shortcuts");
        Games=r.games;
    }

    // The example entry in docs\GAMES.md is what people will copy, so it has to load.
    {
        std::string guide=slurp(root/"docs"/"GAMES.md");
        guide.erase(std::remove(guide.begin(),guide.end(),'\r'),guide.end());   // a Windows checkout turns the guide's line ends into CRLF
        const auto from=guide.find("```ini\n"),to=guide.find("\n```",from==std::string::npos ? 0 : from+6);
        CHECK(from!=std::string::npos && to!=std::string::npos,"the guide has an example entry");
        if (from!=std::string::npos && to!=std::string::npos) {
            const auto sections=parseIni(guide.substr(from+7,to-from-7));
            GameInfo g; std::string why=sections.size()==1 ? gameFromSection(sections[0],"user",g) : std::string("expected one section");
            CHECK(why.empty() && g.id=="example-game" && g.steamAppId==999001 && g.machine=="x64" && g.keys.size()==2 && !g.hasFix,"the guide's example entry is valid ("+why+")");
        }
    }

    // --- Finding games, profiles and settings ----------------------------------------------------------------------
    {
        CHECK(findGame("blindforest")==0 && findGame("wotw")==1 && findGame("nope")==-1 && findGame("")==-1,"games are found by id");
        CHECK(findGameByApp(387290)==0 && findGameByApp(1057090)==1 && findGameByApp(5)==-1,"and by Steam app");
        const fs::path main="C:\\proj\\flattodepth.ini";
        CHECK(profilePath(main,0)==fs::path("C:\\proj\\flattodepth-blindforest.ini") && profilePath(main,1)==fs::path("C:\\proj\\flattodepth-wotw.ini"),"each game has its own profile next to the main config");
        CHECK(profilePath("flattodepth.ini",1)==fs::path("flattodepth-wotw.ini"),"relative config paths work");
        const auto dir=temp/"settings"; fs::create_directories(dir);
        writeFile(dir/"flattodepth.default.ini","[screen]\nswap_eyes=1\ngeneric=1\n"); writeFile(dir/"flattodepth-wotw.default.ini","[screen]\nswap_eyes=0\n");
        ensureSettings(dir/"flattodepth.ini"); ensureSettings(dir/"flattodepth-wotw.ini"); ensureSettings(dir/"flattodepth-hollow-knight.ini");
        CHECK(slurp(dir/"flattodepth.ini")=="[screen]\nswap_eyes=1\ngeneric=1\n","the main settings are created from flattodepth.default.ini");
        CHECK(slurp(dir/"flattodepth-wotw.ini")=="[screen]\nswap_eyes=0\n","a game with defaults of its own gets them");
        CHECK(slurp(dir/"flattodepth-hollow-knight.ini")=="[screen]\nswap_eyes=1\ngeneric=1\n","a game without any starts from the generic defaults");
        writeFile(dir/"flattodepth.ini","[screen]\nswap_eyes=0\npicture_width_m=9\n");   // the user's edits and saved placement
        ensureSettings(dir/"flattodepth.ini");
        CHECK(slurp(dir/"flattodepth.ini")=="[screen]\nswap_eyes=0\npicture_width_m=9\n","an existing settings file is never overwritten");
        fs::remove(dir/"flattodepth.default.ini"); ensureSettings(dir/"flattodepth-other.ini");
        CHECK(!fs::exists(dir/"flattodepth-other.ini"),"no defaults at all, no settings file (built-in defaults apply)");
        CHECK(fs::exists(root/"flattodepth.default.ini"),"the generic defaults ship with the program");
    }

    // --- Steam's records -------------------------------------------------------------------------------------------
    {
        const std::string vdf=
            "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t\t\"label\"\t\t\"\"\n\t}\n"
            "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"E:\\\\SteamLibrary\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"264710\"\t\t\"7038266148\"\n\t\t}\n\t}\n}\n";
        const auto paths=parseLibraryPaths(vdf);
        CHECK(paths.size()==2 && paths[0]=="C:\\Program Files (x86)\\Steam" && paths[1]=="E:\\SteamLibrary","library paths are read and unescaped");
        CHECK(parseLibraryPaths("").empty(),"empty file gives no libraries");
        SteamApp a;
        CHECK(parseAppManifest(manifest(387290,"Ori and the Blind Forest: Definitive Edition","Ori DE"),a) && a.appId==387290 && a.name=="Ori and the Blind Forest: Definitive Edition" && a.installDir=="Ori DE" && a.stateFlags==4 && a.installed(),
            "an app record: id, name, folder and state (the name is the app's, not UserConfig's)");
        CHECK(parseAppManifest("\"AppState\"\n{\n\t\"appid\"\t\t\"5\"\n\t\"installdir\"\t\t\"Five\"\n}\n",a) && a.stateFlags==0 && a.installed() && a.name.empty(),"a record without a state counts as installed");
        CHECK(parseAppManifest(manifest(8,"Say \\\"hi\\\"","Eight"),a) && a.name=="Say \"hi\"","escaped quotes in a name");
        CHECK(parseAppManifest(manifest(9,"Nine","Nine",1026),a) && !a.installed(),"a game still downloading is not installed");
        CHECK(parseAppManifest(manifest(10,"Ten","Ten",6),a) && a.installed(),"fully installed with another flag set is installed");
        CHECK(parseAppManifest(manifest(11,"Eleven","Eleven",1030),a) && a.installed(),"installed and updating is installed");
        CHECK(!parseAppManifest(manifest(12,"Twelve","..\\..\\Windows"),a) && !parseAppManifest(manifest(12,"Twelve","a/b"),a) && !parseAppManifest(manifest(12,"Twelve","C:\\x"),a) &&
            !parseAppManifest(manifest(12,"Twelve",".."),a),"an install folder that would leave steamapps\\common is refused");
        CHECK(!parseAppManifest("",a) && !parseAppManifest("\"AppState\"{}",a) && !parseAppManifest("\"appid\" \"abc\" \"installdir\" \"x\"",a) && !parseAppManifest("\"appid\" \"0\" \"installdir\" \"x\"",a) &&
            !parseAppManifest("\"appid\" \"99999999999\" \"installdir\" \"x\"",a) && !parseAppManifest("\"appid\" \"3\"",a),"records that are not usable are refused");
        SteamApp placed; placed.appId=3; placed.installDir="Folder Name"; placed.library="D:\\Lib";
        CHECK(placed.directory()==fs::path("D:\\Lib")/"steamapps"/"common"/"Folder Name","the game's folder");
    }
    // The libraries, and scanning them.
    const auto libA=temp/"LibA",libB=temp/"LibB",libEmpty=temp/"LibEmpty",libMissing=temp/"LibMissing";
    {
        writeFile(libA/"steamapps"/"appmanifest_100.acf",manifest(100,"Alpha","Alpha"));
        writeFile(libA/"steamapps"/"appmanifest_300.acf",manifest(300,"Charlie","Charlie",1026));
        writeFile(libA/"steamapps"/"readme.txt","not a manifest"); writeFile(libA/"steamapps"/"appmanifest_999.acf","garbage");
        writeFile(libB/"steamapps"/"appmanifest_200.acf",manifest(200,"Bravo","Bravo"));
        writeFile(libB/"steamapps"/"appmanifest_100.acf",manifest(100,"Alpha (leftover)","AlphaOld"));   // a stale record of the same game
        fs::create_directories(libEmpty/"steamapps");
        const auto apps=scanSteamApps({libA,libB,libEmpty,libMissing});
        CHECK(apps.size()==3 && apps[0].appId==100 && apps[1].appId==200 && apps[2].appId==300,"every library is read, in app order; junk and missing folders are ignored");
        CHECK(apps[0].name=="Alpha" && apps[0].library==libA,"a game recorded twice is taken from the first library");
        CHECK(findSteamApp(apps,200) && findSteamApp(apps,200)->name=="Bravo" && !findSteamApp(apps,201),"lookup by app");
        CHECK(!findSteamApp(apps,300)->installed(),"a download in progress is listed but not installed");
        CHECK(scanSteamApps({}).empty() && scanSteamApps({libMissing}).empty(),"nothing to scan");
        // The environment override, as the install scripts have.
        SetEnvironmentVariableW(L"FLATTODEPTH_STEAM_LIBRARIES",(libA.wstring()+L";;"+libB.wstring()+L";").c_str());
        const auto libs=steamLibraries();
        CHECK(libs.size()==2 && libs[0]==libA.lexically_normal() && libs[1]==libB.lexically_normal(),"FLATTODEPTH_STEAM_LIBRARIES overrides Steam's own record; empty entries are skipped");
        SetEnvironmentVariableW(L"FLATTODEPTH_STEAM_LIBRARIES",nullptr);
    }

    // --- Reading executables ---------------------------------------------------------------------------------------
    {
        const auto dir=temp/"pe";
        buildPe(dir/"a32.exe",false,{"KERNEL32.dll","D3D11.dll","XInput1_3.dll"});
        buildPe(dir/"a64.exe",true,{"kernel32.dll","d3d9.dll"},{"opengl32.dll","vulkan-1.dll"});
        const auto a=readPe(dir/"a32.exe"),b=readPe(dir/"a64.exe");
        CHECK(a.valid && !a.x64 && a.imports==std::set<std::string>({"kernel32.dll","d3d11.dll","xinput1_3.dll"}),"a 32-bit executable and its imports, lower-cased");
        CHECK(b.valid && b.x64 && b.imports==std::set<std::string>({"kernel32.dll","d3d9.dll","opengl32.dll","vulkan-1.dll"}),"a 64-bit executable, with its delay-loaded imports too");
        buildPe(dir/"none.exe",true,{}); CHECK(readPe(dir/"none.exe").valid && readPe(dir/"none.exe").imports.empty(),"no imports is fine");
        // Not executables: nothing is read, nothing crashes.
        writeFile(dir/"text.exe","this is not a program at all"); writeFile(dir/"empty.exe",""); writeFile(dir/"mz.exe","MZ");
        std::string truncated(0x90,'\0'); truncated[0]='M'; truncated[1]='Z'; truncated[0x3c]=static_cast<char>(0x80); truncated[0x80]='P'; truncated[0x81]='E'; writeFile(dir/"trunc.exe",truncated);
        for (const char* name : {"text.exe","empty.exe","mz.exe","trunc.exe","missing.exe"}) CHECK(!readPe(dir/name).valid && readPe(dir/name).imports.empty(),std::string("not an executable: ")+name);
        // A hostile import table: more descriptors than the file holds, and names pointing outside any section.
        { std::ifstream in(dir/"a32.exe",std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(in)),{});
          bytes[0x400+12]=static_cast<char>(0xff); bytes[0x400+13]=static_cast<char>(0xff); bytes[0x400+14]=static_cast<char>(0x7f);   // first name RVA far outside
          writeFile(dir/"hostile.exe",bytes); const auto h=readPe(dir/"hostile.exe"); CHECK(h.valid && h.imports.size()<=2,"a name that points outside the file is skipped, not followed"); }
        // Text search.
        const std::string needle="LoadLibraryA(\"D3D11.dll\")";
        buildPe(dir/"loads.exe",true,{"kernel32.dll"},{},needle,300);
        CHECK(fileMentions(dir/"loads.exe","d3d11.dll") && fileMentions(dir/"loads.exe","D3D11.DLL") && !fileMentions(dir/"loads.exe","xinput1_4.dll") && !fileMentions(dir/"nothing.exe","d3d11.dll") && !fileMentions(dir/"loads.exe",""),"text in a program is found, whatever its case");
        // A name that straddles the 1 MiB read boundary is still found.
        { std::string big((1u<<20)-5,'x'); big+="d3d11.dll"; big+=std::string(100,'y'); writeFile(dir/"straddle.bin",big); CHECK(fileMentions(dir/"straddle.bin","d3d11.dll"),"a name across a read boundary is found"); }
        { std::string big(2u<<20,'x'); big+="d3d11.dll"; writeFile(dir/"far.bin",big); CHECK(!fileMentions(dir/"far.bin","d3d11.dll",1u<<20) && fileMentions(dir/"far.bin","d3d11.dll",4u<<20),"the search stops at its limit"); }
    }

    // --- Looking at games ------------------------------------------------------------------------------------------
    {
        const auto lib=temp/"Scan"; const auto common=lib/"steamapps"/"common";
        auto app=[&](unsigned id,const std::string& name,const std::string& dir,unsigned flags=4) { writeFile(lib/"steamapps"/("appmanifest_"+std::to_string(id)+".acf"),manifest(id,name,dir,flags)); return common/dir; };
        // 1. A catalog game (Blind Forest): found by the catalog's executable name.
        buildPe(app(387290,"Ori and the Blind Forest: Definitive Edition","Ori DE")/"oriDE.exe",false,{"kernel32.dll","d3d11.dll"});
        // 2. An unlisted DirectX 11 game, 64-bit, with an XInput import.
        buildPe(app(1001,"Zed's Quest: Dark Edition!","Zeds Quest")/"zed.exe",true,{"kernel32.dll","d3d11.dll","xinput1_4.dll"});
        // 3. A Unity-style game: the program itself is small; the renderer and input are loaded by name from UnityPlayer.dll.
        { const auto d=app(1002,"Alpha Unity","AlphaUnity"); buildPe(d/"alpha.exe",true,{"kernel32.dll","UnityPlayer.dll"}); buildPe(d/"UnityPlayer.dll",true,{"kernel32.dll"},{},"...LoadLibrary d3d11.dll ... XInput1_3.dll ...",100); }
        // 4. DirectX 12 and Vulkan only. 5. DirectX 9 only. 6. No executable at all. 7. Still downloading.
        buildPe(app(1003,"Twelve","Twelve")/"twelve.exe",true,{"d3d12.dll","vulkan-1.dll"});
        buildPe(app(1004,"Old Nine","OldNine")/"nine.exe",false,{"d3d9.dll","xinput9_1_0.dll"});
        writeFile(app(1005,"Data Only","DataOnly")/"data.pak","x");
        buildPe(app(1006,"Downloading","Downloading",1026)/"dl.exe",true,{"d3d11.dll"});
        // 8. An installer and a crash reporter beside the real program, which is smaller than either.
        { const auto d=app(1007,"Real Exe","RealExe"); buildPe(d/"game.exe",true,{"d3d11.dll"}); buildPe(d/"unins000.exe",true,{},{},{},50000); buildPe(d/"CrashHandler.exe",true,{},{},{},60000); buildPe(d/"bin"/"vc_redist.x64.exe",true,{},{},{},70000); }
        // 9. A 32-bit DirectX 11 game that loads xinput1_3, which the 32-bit shim does not cover.
        buildPe(app(1008,"Thirty Two","ThirtyTwo")/"t.exe",false,{"d3d11.dll","xinput1_3.dll"});
        // 10. The game is two folders down, as Unreal games keep it.
        buildPe(app(1009,"Deep One","DeepOne")/"Game"/"Binaries"/"Win64"/"Deep-Win64-Shipping.exe",true,{"d3d11.dll"},{},{},4000);
        // The launcher stub at the top is bigger than nothing but smaller than the real thing.
        buildPe(common/"DeepOne"/"Deep.exe",true,{"kernel32.dll"});

        const auto apps=scanSteamApps({lib});
        std::vector<std::string> seen; const auto results=scanInstalledGames(apps,[&](const SteamApp& a) { seen.push_back(a.name); });
        CHECK(results.size()==9 && seen.size()==9,"every installed game is examined, a download in progress is not");
        auto find=[&](unsigned id) -> const GameAnalysis& { for (const auto& r : results) if (r.app.appId==id) return r; static GameAnalysis none; return none; };
        CHECK(find(387290).verdict==Verdict::Supported && find(387290).catalogGame==0 && !find(387290).x64 && find(387290).dx11,"a catalog game is supported");
        const auto& zed=find(1001);
        CHECK(zed.verdict==Verdict::Candidate && zed.x64 && zed.dx11 && zed.graphics.count("DirectX 11") && zed.xinput==std::set<std::string>({"xinput1_4"}) && zed.catalogGame==-1,"an unlisted DirectX 11 game is a candidate; bitness, graphics and controller DLL are read");
        const auto& unity=find(1002);
        CHECK(unity.verdict==Verdict::Candidate && unity.dx11 && unity.xinput.count("xinput1_3"),"a game that loads DirectX 11 and XInput by name, from its engine DLL, is found too");
        CHECK(find(1003).verdict==Verdict::Unlikely && !find(1003).dx11 && find(1003).graphics.count("DirectX 12") && find(1003).graphics.count("Vulkan"),"DirectX 12 and Vulkan only: unlikely");
        CHECK(find(1004).verdict==Verdict::Unlikely && find(1004).graphics==std::set<std::string>({"DirectX 9"}) && find(1004).xinput.count("xinput9_1_0"),"DirectX 9 only: unlikely");
        CHECK(find(1005).verdict==Verdict::Unknown && find(1005).exe.empty(),"no program: unknown");
        CHECK(find(1006).app.appId==0,"a game still downloading is not in the results");
        CHECK(find(1007).exe.filename()=="game.exe" && find(1007).verdict==Verdict::Candidate,"installers, crash reporters and redistributables are not mistaken for the game");
        CHECK(find(1009).exe.filename()=="Deep-Win64-Shipping.exe" && find(1009).verdict==Verdict::Candidate,"the biggest program within a few folders is the game");
        // Order: supported, candidates by name, then the rest.
        CHECK(results[0].app.appId==387290,"supported games come first");
        std::vector<std::string> candidates; for (const auto& r : results) if (r.verdict==Verdict::Candidate) candidates.push_back(r.app.name);
        CHECK(candidates==std::vector<std::string>({"Alpha Unity","Deep One","Real Exe","Thirty Two","Zed's Quest: Dark Edition!"}),"candidates are sorted by name");
        int lastVerdict=-1; bool ordered=true; for (const auto& r : results) { ordered&=static_cast<int>(r.verdict)>=lastVerdict; lastVerdict=static_cast<int>(r.verdict); } CHECK(ordered,"verdicts never go back up the list");

        // The report.
        const auto brief=scanReport(results,false),full=scanReport(results,true);
        CHECK(contains(brief,"9 installed Steam games: 1 supported, 5 candidates, 3 other"),"the summary counts what was found");
        CHECK(contains(brief,"supported") && contains(brief,"387290") && contains(brief,"Zed's Quest") && contains(brief,"candidate") && !contains(brief,"Twelve") && !contains(brief,"Data Only") && contains(brief,"3 other games hidden"),"by default only games worth a look are listed");
        CHECK(contains(full,"Twelve") && contains(full,"Old Nine") && contains(full,"Data Only") && !contains(full,"hidden"),"--all lists everything");
        CHECK(contains(brief,"x64") && contains(brief,"DirectX 11") && contains(brief,"xinput1_4") && contains(brief,"--draft"),"bits, graphics, controller DLL and how to get a draft");
        { GameAnalysis longName; longName.verdict=Verdict::Candidate; longName.app.appId=5; longName.app.name=std::string(60,'N'); longName.exe="x.exe"; longName.x64=true; longName.dx11=true; longName.graphics={"DirectX 11"};
          const auto line=scanReport({longName},false);
          CHECK(contains(line,std::string(39,'N')+" x64") && !contains(line,std::string(40,'N')),"a name too long for its column is cut short and keeps a gap before the next column"); }
        CHECK(contains(scanReport({},false),"0 installed") && !contains(scanReport({},false),"Candidates draw"),"an empty scan reads sensibly");

        // Draft entries are valid catalog entries, so pasting one in works.
        for (unsigned id : {1001u,1002u,1007u,1008u,1009u}) {
            const std::string draft=draftEntry(find(id));
            const auto sections=parseIni(draft);
            GameInfo g; std::string why;
            CHECK(sections.size()==1 && (why=gameFromSection(sections[0],"user",g)).empty(),"the draft for "+find(id).app.name+" is a valid entry ("+why+")");
            CHECK(g.steamAppId==id && g.process==widen(find(id).exe.filename().string()) && g.machine==(find(id).x64 ? "x64" : "x86") && !g.hasFix,"and says what it knows");
        }
        const std::string zedDraft=draftEntry(zed);
        CHECK(contains(zedDraft,"[zed-s-quest-dark-edition]") && contains(zedDraft,"title=Zed's Quest: Dark Edition!") && contains(zedDraft,"exe=zed.exe") && contains(zedDraft,"shim_files=xinput1_4.dll,xinput1_3.dll") && contains(zedDraft,"fix_url"),"a draft names the game, its program and the 64-bit shim, and says where the fix goes");
        const std::string thirty=draftEntry(find(1008));
        CHECK(!contains(thirty,"\nshim_files=") && contains(thirty,"xinput1_3") && contains(thirty,"xinput9_1_0"),"a 32-bit game on another XInput DLL is told the shim does not cover it");
        CHECK(contains(draftEntry(find(1004)),"shim_files=xinput9_1_0.dll"),"a 32-bit game on xinput9_1_0 gets the 32-bit shim");
        // Section names.
        CHECK(slugFor("Hollow Knight",1)=="hollow-knight" && slugFor("  ??? ",77)=="game-77" && slugFor("A -- B",1)=="a-b" && slugFor("Ori and the Will of the Wisps: Extended",1).size()<=28 && slugFor("x",1)=="x","section names from game names");
        const auto longName=slugFor("Ori and the Will of the Wisps: Extended Edition",1); CHECK(longName.back()!='-' && longName.size()<=28,"a long name is cut cleanly");
        // Finding the program.
        CHECK(findGameExe(common/"RealExe",L"").filename()=="game.exe" && findGameExe(common/"Ori DE",L"oriDE.exe").filename()=="oriDE.exe" && findGameExe(common/"Nothing",L"").empty() && findGameExe(common/"Ori DE",L"missing.exe").filename()=="oriDE.exe",
            "the catalog's executable is used when it is there, otherwise the biggest sensible one");
    }

    fs::remove_all(temp,ignored);
    if (failures) { std::cerr<<failures<<" catalog check(s) failed\n"; return 1; }
    std::cout<<"PASS games catalog, Steam scan, executable reader, scan report and draft entries\n";
    return 0;
}
