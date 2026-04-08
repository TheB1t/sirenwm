#pragma once
// Bar render state — plain data, no backend window-object dependencies.
// Core builds this snapshot and passes it to BarWindow::draw().

#include <string>
#include <vector>

struct BarTag {
    int         id;
    std::string name;
    bool        focused;
    bool        has_windows;
    bool        urgent;
};

struct BarState {
    std::vector<BarTag> tags;
    std::string         title;      // focused window title
};
