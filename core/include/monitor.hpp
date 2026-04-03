#pragma once

#include <string>
#include <memory>

struct Workspace;

struct Monitor {
    int         id;
    std::string name;

    int         x, y;
    int         width, height;

    int         active_ws = -1;

    Monitor(int id, std::string name, int x, int y, int width, int height)
        : id(id), name(std::move(name)), x(x), y(y), width(width), height(height) {}
};

using MonitorState = Monitor;