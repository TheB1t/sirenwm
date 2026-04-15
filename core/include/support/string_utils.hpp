#pragma once

#include <string>

// ASCII-only lowercase — used for case-insensitive comparisons in config
// parsing and keybinding tokenisation.
inline std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
    return s;
}

// Returns true if rot is one of the four valid XRandR rotation values.
inline bool is_valid_rotation(const std::string& rot) {
    auto r = lower_ascii(rot);
    return r == "normal" || r == "left" || r == "right" || r == "inverted";
}
