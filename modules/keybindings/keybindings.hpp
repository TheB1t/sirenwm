#pragma once

#include <lua_host.hpp>
#include <module.hpp>
#include <vec.hpp>

namespace backend { class InputPort; }

class KeybindingsModule : public Module {
    public:
        explicit KeybindingsModule(ModuleDeps deps) : Module(deps) {}
        std::string name() const override { return "keybindings"; }
        void on_init()   override;
        void on_lua_init() override;
        void on_start()  override;
        void on_stop(bool is_exec_restart = false) override;
        void on_reload() override;
        void on(event::KeyPressEv) override;
        void on(event::ButtonEv) override;
        void on(event::MotionEv) override;
        void on(event::FocusChanged) override;
        void on(event::WindowMapped) override;
        void on(event::WindowUnmapped) override;
        void on(event::DestroyNotify) override;
        ~KeybindingsModule() override;

    private:
        enum class DragOp { None, Move, Resize };
        struct DragState {
            DragOp   op     = DragOp::None;
            WindowId window = NO_WINDOW;
            Vec2i    start_root;      // root pointer pos at drag start
            Vec2i    start_win_pos;   // window pos at drag start
            Vec2i    start_win_size;  // window size at drag start
        };

        void stop_drag();
        bool drag_active() const { return drag.op != DragOp::None && drag.window != NO_WINDOW; }
        void maybe_cancel_drag_for_window(WindowId window);

        // Pending Lua refs set via kb.binds/kb.mouse during init.lua execution.
        // Applied in on_start()/on_reload() after siren.modifier is resolved.
        LuaRegistryRef pending_binds_;
        LuaRegistryRef pending_mouse_;

        void apply_pending();

        backend::InputPort* input_          = nullptr;
        WindowId            focused_window_ = NO_WINDOW;
        DragState           drag;
};
