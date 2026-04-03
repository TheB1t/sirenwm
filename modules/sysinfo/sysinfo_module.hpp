#pragma once

#include <module.hpp>

class SysinfoModule : public Module {
    public:
        explicit SysinfoModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "sysinfo"; }
        void on_init(Core&)     override;
        void on_lua_init(Core&) override;
        void on_start(Core&)    override;
        void on_stop(Core&, bool is_exec_restart = false) override;
};