#pragma once

#include <module.hpp>
#include <string>
#include <vector>

class KeyboardModule : public Module {
    public:
        explicit KeyboardModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "keyboard"; }
        void on_init(Core&)     override;
        void on_lua_init(Core&) override;
        void on_start(Core&)    override;
        void on_reload(Core&)   override;
        void on_stop(Core&, bool is_exec_restart) override;

    private:
        std::vector<std::string> layouts_;
        std::string options_;

        void apply();
};