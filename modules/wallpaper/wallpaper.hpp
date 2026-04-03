#pragma once

#include <module.hpp>
#include <string>
#include <unordered_map>

struct WallpaperEntry {
    std::string image;
    std::string mode; // "stretch", "center", "zoom", "tile"
};

class WallpaperModule : public Module {
    public:
        explicit WallpaperModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "wallpaper"; }
        void on_init(Core&)                          override;
        void on_lua_init(Core&)                      override;
        void on_start(Core&)                         override;
        void on_reload(Core&)                        override;
        void on(Core&, event::DisplayTopologyChanged) override;

    private:
        std::unordered_map<std::string, WallpaperEntry> entries_; // alias -> entry

        void apply();
};