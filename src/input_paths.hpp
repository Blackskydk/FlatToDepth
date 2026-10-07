#pragma once
#include <string>

// OpenXR paths FlatToDepth binds, as plain strings so a test can check them. One of them was once wrong (the haptic output carried an extra
// /input/), and the runtime's only answer to a path that does not exist is to refuse the whole suggestion, which looked like "no rumble".
namespace paths {
inline std::string hand(const std::string& side) { return "/user/hand/"+side; }
inline std::string input(const std::string& side,const std::string& name) { return hand(side)+"/input/"+name; }
inline std::string haptic(const std::string& side) { return hand(side)+"/output/haptic"; }
}
