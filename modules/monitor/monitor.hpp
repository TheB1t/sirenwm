#pragma once

#include <module.hpp>

class MonitorModule : public Module {
    public:
        explicit MonitorModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "monitors"; }
        void on_init(Core& core) override;
        void on_start(Core& core) override;
};