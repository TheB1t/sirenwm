#pragma once

#include <runtime/module.hpp>
#include <backend/events.hpp>
#include <string>
#include <vector>
#include <unordered_map>

class KeyboardModule : public Module {
    public:
        explicit KeyboardModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "keyboard"; }
        void on_init()     override;
        void on_lua_init() override;
        void on_start()    override;
        void on_reload()   override;
        void on_stop(bool is_exec_restart) override;

        void on(event::FocusChanged ev)  override;
        void on(event::WindowUnmapped ev) override;

        bool parse_setup(struct LuaContext& lua, int idx, std::string& err);

    private:
        std::vector<std::string> layouts_;
        std::string options_;

        // Per-window XKB group tracking.
        std::unordered_map<WindowId, uint32_t> window_groups_;
        WindowId focused_window_ = NO_WINDOW;

        void apply();
};
