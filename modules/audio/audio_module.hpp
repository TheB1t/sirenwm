#pragma once

#include <runtime/module.hpp>

class AudioModule : public Module {
    public:
        explicit AudioModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "audio"; }
        void on_init()     override;
        void on_lua_init() override;
        void on_start()    override;
        void on_stop(bool is_exec_restart) override;
};
