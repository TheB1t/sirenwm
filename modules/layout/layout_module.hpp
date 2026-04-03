#pragma once

#include <module.hpp>

class LayoutModule : public Module {
    public:
        explicit LayoutModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "layout"; }
        void on_init(Core& core) override;
        void on_lua_init(Core& core) override;
};