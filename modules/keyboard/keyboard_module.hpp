#pragma once

#include <module.hpp>
#include <string>
#include <vector>

class KeyboardModule : public Module {
    public:
        explicit KeyboardModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "keyboard"; }
        void on_init()     override;
        void on_lua_init() override;
        void on_start()    override;
        void on_reload()   override;
        void on_stop(bool is_exec_restart) override;

        bool parse_setup(struct LuaContext& lua, int idx, std::string& err);

    private:
        std::vector<std::string> layouts_;
        std::string options_;

        void apply();
};
