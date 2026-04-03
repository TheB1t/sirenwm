#pragma once
#include <module.hpp>

class RulesModule : public Module {
    public:
        explicit RulesModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "rules"; }
        void on_init(Core&) override;
        void on_lua_init(Core&) override;
        void on(Core&, event::ApplyWindowRules) override;
};