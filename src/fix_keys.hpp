#pragma once
#include "catalog.hpp"
#include <map>
#include <sstream>

// What a stereo fix's shortcut keys really do, read from the fix's own d3dx.ini in the game folder. A Geo-11 fix binds F-keys to
// settings: a key of type cycle steps through a list of presets (convergence = 0, 15, 28, 38.6) and wraps round, a key of type
// toggle flips a value, and a cycle of two values is a toggle in all but name. The tools panel shows how many steps a button has
// and, from the presses it has made itself, which one it is on, and explains the rest in words.
namespace fixkeys {
enum class Type { Unknown, Cycle, Toggle };
struct Key {
    WORD vk=0;                          // F1..F12: the key the panel presses
    Type type=Type::Unknown;
    std::string variable;               // what the key sets: convergence, x10, y1 ...
    std::vector<std::string> values;    // a cycle's presets, as the fix writes them; a toggle's one alternative
    std::string initial;                // the value the fix starts that variable at, if it says
    bool hasBack=false;                 // Shift+key steps backwards
    std::string comment;                // the fix author's note on the line above the section
    int startIndex=-1;                  // where in `values` the starting value is, when it is one of them
    bool toggleLike() const { return type==Type::Toggle || (type==Type::Cycle && values.size()==2); }
    size_t steps() const { return type==Type::Cycle ? values.size() : 2; }
};

inline bool functionKey(std::string token,WORD& vk) {
    token=lowerCase(token);
    if (token.rfind("vk_",0)==0) token=token.substr(3);
    if (token.size()<2 || token.size()>3 || token[0]!='f') return false;
    for (size_t i=1;i<token.size();++i) if (!std::isdigit(static_cast<unsigned char>(token[i]))) return false;
    const int n=std::stoi(token.substr(1));
    if (n<1 || n>12) return false;
    vk=static_cast<WORD>(VK_F1+n-1); return true;
}
inline bool sameNumber(const std::string& a,const std::string& b) {
    if (a==b) return true;
    try { size_t i=0,j=0; const double x=std::stod(a,&i),y=std::stod(b,&j); return i==a.size() && j==b.size() && std::fabs(x-y)<1e-9; } catch (const std::exception&) { return false; }
}

// Reads the [Key...] sections of a d3dx.ini. Only keys bound to a plain function key are returned (the panel can press nothing else).
inline std::vector<Key> parse(const std::string& text) {
    static const char* const reserved[]{"transition","transition_type","release_transition","release_transition_type","release_delay","delay","wrap","smart","condition","run"};
    std::vector<Key> keys; std::map<std::string,std::string> startValues;
    Key current; bool inKey=false,haveSection=false,justAfterComment=false; std::string comment;
    auto finish=[&]() { if (haveSection && current.vk) keys.push_back(current); haveSection=false; current=Key{}; };
    std::istringstream in(text);
    for (std::string raw;std::getline(in,raw);) {
        const std::string line=trimmed(raw);
        if (line.empty()) { justAfterComment=false; continue; }
        if (line[0]==';') { comment=trimmed(line.substr(1)); justAfterComment=true; continue; }
        if (line[0]=='[') {
            finish();
            const auto close=line.find(']');
            const std::string name=lowerCase(line.substr(1,close==std::string::npos ? std::string::npos : close-1));
            inKey=name.rfind("key",0)==0;
            if (inKey) { haveSection=true; current=Key{}; if (justAfterComment) current.comment=comment; }
            justAfterComment=false; continue;
        }
        justAfterComment=false;
        const auto eq=line.find('=');
        if (eq==std::string::npos) continue;
        const std::string name=lowerCase(trimmed(line.substr(0,eq)));
        std::string value=line.substr(eq+1);
        if (const auto semicolon=value.find(';'); semicolon!=std::string::npos) value.resize(semicolon);
        value=trimmed(value);
        if (!inKey) { startValues[name]=value; continue; }
        if (name=="key") {
            std::istringstream words(value); std::string word,last; while (words>>word) last=word;
            WORD vk=0; if (!current.vk && functionKey(last,vk)) current.vk=vk;
        }
        else if (name=="back") current.hasBack=true;
        else if (name=="type") { const std::string t=lowerCase(value); current.type=t=="cycle" ? Type::Cycle : t=="toggle" ? Type::Toggle : Type::Unknown; }
        else if (std::find_if(std::begin(reserved),std::end(reserved),[&](const char* r) { return name==r; })!=std::end(reserved)) continue;
        else if (current.variable.empty()) { current.variable=name; current.values=splitList(value); }
    }
    finish();
    for (auto& key : keys) {
        const auto start=startValues.find(key.variable);
        if (start!=startValues.end()) key.initial=start->second;
        if (key.type==Type::Cycle && !key.initial.empty())
            for (size_t i=0;i<key.values.size();++i) if (sameNumber(key.values[i],key.initial)) { key.startIndex=static_cast<int>(i); break; }
    }
    return keys;
}
inline const Key* find(const std::vector<Key>& keys,WORD vk) {
    for (const auto& k : keys) if (k.vk==vk) return &k;
    return nullptr;
}

// Where a cycle is after this many presses made through the panel: 0-based, or -1 if that is not known. The fix resynchronises a cycle
// to the preset the variable really holds before it steps ("smart" cycles, the default), so counting from the starting value is
// right as long as only the panel presses the key; a press on a keyboard is not seen.
inline int cycleIndex(const Key& k,int presses) {
    const int n=static_cast<int>(k.values.size());
    if (k.type!=Type::Cycle || n<1) return -1;
    if (k.startIndex>=0) return (k.startIndex+presses)%n;
    return presses>0 ? (presses-1)%n : -1;    // the first press sets the first preset
}
// The short note on a button next to its key: how many steps it has, or which one it is on; for a toggle, whether it has been flipped.
inline std::wstring caption(const Key* k,int presses) {
    if (!k || k->type==Type::Unknown) return {};
    if (k->toggleLike()) return presses==0 ? L"toggle" : (presses%2 ? L"switched" : L"normal");
    const int at=cycleIndex(*k,presses);
    const std::wstring steps=std::to_wstring(k->values.size());
    return at<0 ? steps+L" steps" : std::to_wstring(at+1)+L"/"+steps;
}
}
