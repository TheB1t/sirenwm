#pragma once

#include <module.hpp>

namespace backend { class InputPort; }

class KeybindingsModule : public Module {
    public:
        explicit KeybindingsModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "keybindings"; }
        void on_init(Core&)   override;
        void on_lua_init(Core&) override;
        void on_start(Core&)  override;
        void on_stop(Core&, bool is_exec_restart = false) override;
        void on_reload(Core&) override;
        void on(Core&, event::KeyPressEv) override;
        void on(Core&, event::ButtonEv) override;
        void on(Core&, event::MotionEv) override;
        void on(Core&, event::WindowMapped) override;
        void on(Core&, event::WindowUnmapped) override;
        void on(Core&, event::DestroyNotify) override;
        ~KeybindingsModule() override;

    private:
        enum class DragOp { None, Move, Resize };
        struct DragState {
            DragOp   op       = DragOp::None;
            WindowId window   = NO_WINDOW;
            int      start_rx = 0;
            int      start_ry = 0;
            int      start_wx = 0;
            int      start_wy = 0;
            int      start_ww = 0;
            int      start_wh = 0;
        };

        void stop_drag();
        bool drag_active() const { return drag.op != DragOp::None && drag.window != NO_WINDOW; }
        void maybe_cancel_drag_for_window(WindowId window);

        backend::InputPort* input_ = nullptr;
        DragState drag;
};