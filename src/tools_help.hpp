#pragma once
#include "fix_keys.hpp"
#include <cwctype>
#include "tools_panel.hpp"

// The words the tools panel shows about the button a laser is on: what the setting is, and how pressing it behaves. For a game's own
// shortcuts the second half comes from the fix's d3dx.ini (see fix_keys.hpp), so it is true for that fix and not a guess.
namespace tools {
inline const wchar_t* IdleHelp=L"Point at a button to see what it does. Grab the bar underneath to carry this panel; the thumbstick pushes it away or pulls it closer.";
inline const wchar_t* BarHelp=L"Hold the trigger or a grip on this bar and move your hand: the panel follows it, and turns to face you. The thumbstick pushes it away or pulls it closer. It opens where you leave it.";
inline const wchar_t* CloseHelp=L"Closes this panel. Both grips and B opens and closes it from anywhere.";

inline bool mentions(std::wstring label,const wchar_t* word) { for (auto& c : label) c=static_cast<wchar_t>(towlower(c)); return label.find(word)!=std::wstring::npos; }

// What a setting is, by the name the catalog gives its button (the same fix setting has the same meaning in every game). When the name
// is not known, the fix author's own note is the best there is.
inline std::wstring glossary(const std::wstring& label,const std::string& comment) {
    if (mentions(label,L"convergence")) return L"Convergence sets where the 3D screen plane sits. Things nearer than it pop out in front of the screen, things beyond it sink behind. Too far either way is tiring for the eyes.";
    if (mentions(label,L"hud depth")) return L"How deep the on-screen display (health, icons, text) sits in 3D. The presets run from shallow to full depth; 0 lays it flat on the screen. If it looks doubled or uncomfortable, try another step.";
    if (mentions(label,L"depth of field") || mentions(label,L"blur")) return L"The blur on things that are out of focus. 3D fixes often switch it off, because blur can make depth look wrong.";
    if (mentions(label,L"hud")) return L"Hides or shows the on-screen display (health, icons, text). Handy if it looks wrong in 3D.";
    if (mentions(label,L"vignette")) return L"The darkened edges of the picture. Switch it off if the dark corners distract in 3D.";
    if (mentions(label,L"bloom")) return L"The soft glow around bright things. Switch it off if the glow looks blurry or doubled in 3D.";
    if (mentions(label,L"grain")) return L"The grainy noise laid over the picture. Switching it off gives a cleaner picture in 3D.";
    return comment.empty() ? std::wstring() : widen(comment);
}
inline std::wstring joinValues(const std::vector<std::string>& values) {
    std::wstring out;
    for (size_t i=0;i<values.size();++i) { if (i) out+=L" \u2192 "; out+=widen(values[i]); }
    return out;
}
// The whole text for one of a game's shortcut buttons. `key` is what the fix says about it (null if its d3dx.ini could not be read).
inline std::wstring explainKey(const std::wstring& label,WORD vk,const fixkeys::Key* key,int presses) {
    const std::wstring name=L"F"+std::to_wstring(vk-VK_F1+1);
    std::wstring text=glossary(label,key ? key->comment : std::string());
    if (!key || key->type==fixkeys::Type::Unknown) return text.empty() ? label+L": presses "+name+L" for the game's 3D fix." : text+L" Presses "+name+L" for the fix.";
    if (!text.empty()) text+=L" ";
    if (key->toggleLike()) {
        text+=L"Each press flips it between its two states.";
    } else {
        text+=L"Each press goes to the next of "+std::to_wstring(key->values.size())+L" presets: "+joinValues(key->values)+L", then back to the first.";
        const int at=fixkeys::cycleIndex(*key,presses);
        if (at>=0) text+=L" Now: step "+std::to_wstring(at+1)+L" ("+widen(key->values[static_cast<size_t>(at)])+L").";
    }
    if (key->hasBack) text+=L" Shift+"+name+L" on a keyboard steps back.";
    return text;
}
// The words for FlatToDepth's own buttons.
inline std::wstring explainSetting(Kind kind,bool curveAvailable,bool rumbleAvailable) {
    switch (kind) {
    case Kind::SwapEyes: return L"If the depth looks inside-out (far things pop out, near things sink), swap which eye sees which picture. Kept for this game.";
    case Kind::Curve: return curveAvailable ? L"Bends the screen around you. Each press goes one step further: Off, Low, Medium, High, then Off again. The dots show the step." :
        L"A curved screen does not work on this PC or runtime.";
    case Kind::Glow: return L"A soft wash of the picture's own colours around the screen, instead of a black room. On or off.";
    case Kind::FloatWindow: return L"Hides a thin sliver at the edge of each eye's picture, so the screen's edges look nearer than the picture. That keeps things that pop out of the screen from being cut off by it. Off, Low, Medium, High.";
    case Kind::Rumble: return rumbleAvailable ? L"The game's vibration, played on your controllers. On or off." :
        L"The game's vibration, played on your controllers. This runtime offers no vibration for them.";
    case Kind::Recenter: return L"Puts the screen, and this panel, back in front of you. Both grips and A does the same.";
    case Kind::Key: break;
    }
    return {};
}
}
