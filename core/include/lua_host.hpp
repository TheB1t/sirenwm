#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <event_receiver.hpp>

class Core;

class LuaContext {
    void* state_ = nullptr;

    public:
        LuaContext() = default;
        explicit LuaContext(void* state) : state_(state) {}

        bool valid() const { return state_ != nullptr; }

        int         abs_index(int idx) const;
        bool        is_nil(int idx) const;
        bool        is_bool(int idx) const;
        bool        is_integer(int idx) const;
        int         arg_count() const;
        bool        is_number(int idx) const;
        bool        is_string(int idx) const;
        bool        is_table(int idx) const;
        bool        is_function(int idx) const;

        bool        to_bool(int idx) const;
        int64_t     to_integer(int idx) const;
        double      to_number(int idx) const;
        std::string to_string(int idx) const;

        std::string check_string(int idx) const;
        int64_t     check_integer(int idx) const;
        double      check_number(int idx) const;
        void        check_table(int idx) const;
        void        check_function(int idx) const;

        int         raw_len(int idx) const;
        void        push_nil() const;
        bool        next(int idx) const; // key at top -> pushes next key/value, returns true if value pushed
        void        raw_geti(int idx, int n) const;
        void        get_field(int idx, const char* key) const;
        void        set_field(int idx, const char* key) const;
        void        raw_get(int idx) const;
        void        raw_set(int idx) const;
        void        raw_seti(int idx, int n) const;
        void        push_value(int idx) const;
        void        push_integer(int64_t v) const;
        void        push_number(double v) const;
        void        push_string(const std::string& v) const;
        void        push_bool(bool v) const;
        void        new_table() const;
        void        get_global(const char* name) const;
        void        set_global(const char* name) const;
        void        set_metatable(int idx) const;
        bool        new_metatable(const char* name) const;
        void        get_metatable_reg(const char* name) const;
        void*       new_userdata(size_t size) const;
        void*       to_userdata(int idx) const;
        bool        is_userdata(int idx) const;
        void        pop(int n = 1) const;
        void        remove(int idx) const;
        void        insert(int idx) const;

        int         errorf(const char* fmt, ...) const;
};

using LuaNativeFn            = int (*)(LuaContext&);
using LuaNativeFnWithContext = int (*)(LuaContext&, void*);
// Signature for a function that pushes Lua arguments for a WM event onto the stack.
// Returns the number of values pushed.
using LuaEventPushFn = int (*)(LuaContext&, const void* ev, Core&);

struct LuaFunctionReg {
    const char* name = nullptr;
    LuaNativeFn func = nullptr;
};

class LuaRegistryRef {
    int      ref_      = -1;
    uint64_t vm_epoch_ = 0;
    friend class LuaHost;

    public:
        LuaRegistryRef() = default;
        bool valid() const { return ref_ >= 0; }
        int raw_ref() const { return ref_; }
};

class LuaHost : public IEventReceiver {
    void*    state_    = nullptr;
    uint64_t vm_epoch_ = 0;
    Core&    core_;

    // siren.on handlers: event name → list of Lua function refs
    std::unordered_map<std::string, std::vector<LuaRegistryRef>> event_handlers_;

    // Module API tables registered by C++ modules via set_module_table().
    std::unordered_map<std::string, LuaRegistryRef> module_tables_;

    public:
        explicit LuaHost(Core& core) : core_(core) {}
        ~LuaHost() override;

        LuaHost(const LuaHost&)            = delete;
        LuaHost& operator=(const LuaHost&) = delete;

        void     init();
        bool initialized() const { return state_ != nullptr; }
        void     reset_root_table();
        LuaContext context() const { return LuaContext(state_); }

        // Bring all base on() overloads into scope so the overrides below
        // don't hide them.
        using IEventReceiver::on;

        // IEventReceiver overrides — Lua-exposed events
        void on(event::WindowMapped ev)              override;
        void on(event::WindowUnmapped ev)            override;
        void on(event::FocusChanged ev)              override;
        void on(event::WorkspaceSwitched ev)         override;
        void on(event::ApplyWindowRules ev)          override;
        void on(event::DisplayTopologyChanged ev)    override;
        void on(event::RuntimeStarted ev)            override;
        void on(event::RuntimeStopping ev)           override;
        void on(event::ConfigReloaded ev)            override;
        void on(event::ChildExited ev)               override;
        void on(event::WindowAssignedToWorkspace ev) override;
        void on(const event::CustomEvent& ev)        override;

        // Register siren.on(event, fn) from Lua — called by the C++ siren.on binding.
        void register_handler(const std::string& event, LuaRegistryRef handler);
        // Clear all handlers — call before reload so refs don't accumulate.
        void clear_handlers();
        // Emit event to Lua handlers. push_fn pushes arguments onto the stack.
        void emit_to_lua(const std::string& event,
            LuaEventPushFn push_fn = nullptr,
            const void* ev         = nullptr,
            Core* core             = nullptr);

        void register_namespace(const std::string& name,
            std::initializer_list<LuaFunctionReg> funcs);
        void register_global(std::initializer_list<LuaFunctionReg> funcs);
        void ensure_table(const std::string& dotted_path);
        void push_callback(LuaNativeFn fn) const;
        void push_callback(LuaNativeFnWithContext fn, void* userdata) const;

        bool exec_file(const std::string& path);
        bool exec_string(const char* code, const char* name = "=prelude");

        // Module table registry: a module calls set_module_table(name) in on_lua_init()
        // to publish its API table (top of stack). lua_module_preload then returns it.
        void           set_module_table(const std::string& name);
        bool           push_module_table(const std::string& name) const;

        LuaRegistryRef ref_value(int index) const;
        LuaRegistryRef ref_function(int index) const;
        bool           push_ref(const LuaRegistryRef& ref) const;
        bool           pcall(int nargs, int nresults, const char* context) const;
        bool           call_ref(const LuaRegistryRef& ref, int nargs, int nresults, const char* context) const;
        bool           call_ref_string(const LuaRegistryRef& ref, std::string& out, const char* context) const;
        bool           call_ref_method_string(const LuaRegistryRef& obj_ref, const char* method, std::string& out, const char* context) const;
        bool           call_ref_with_int_fields(const LuaRegistryRef& ref,
            std::initializer_list<std::pair<const char*, int64_t>> fields,
            const char* context) const;
};
