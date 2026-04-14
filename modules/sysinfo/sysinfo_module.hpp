#pragma once

#include <runtime/module.hpp>

namespace backend { class KeyboardPort; }

class SysinfoModule : public Module {
    public:
        explicit SysinfoModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "sysinfo"; }
        void                   on_init()     override;
        void                   on_lua_init() override;
        void                   on_start()    override;
        void                   on_stop(bool is_exec_restart = false) override;

        backend::KeyboardPort& backend_keyboard_port();
};
