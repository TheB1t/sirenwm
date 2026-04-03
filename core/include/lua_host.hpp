#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

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
        void        pop(int n = 1) const;
        void        remove(int idx) const;
        void        insert(int idx) const;

        int         errorf(const char* fmt, ...) const;
};

using LuaNativeFn            = int (*)(LuaContext&);
using LuaNativeFnWithContext = int (*)(LuaContext&, void*);

struct LuaFunctionReg {
    const char* name = nullptr;
    LuaNativeFn func = nullptr;
};

class LuaRegistryRef {
    int ref_           = -1;
    uint64_t vm_epoch_ = 0;
    friend class LuaHost;

    public:
        LuaRegistryRef() = default;
        bool valid() const { return ref_ >= 0; }
        int raw_ref() const { return ref_; }
};

class LuaHost {
    void* state_       = nullptr;
    uint64_t vm_epoch_ = 0;

    public:
        LuaHost() = default;
        ~LuaHost();

        LuaHost(const LuaHost&)            = delete;
        LuaHost& operator=(const LuaHost&) = delete;

        void     init();
        bool initialized() const { return state_ != nullptr; }
        void     reset_root_table();
        LuaContext context() const { return LuaContext(state_); }

        void           register_namespace(const std::string& name,
            std::initializer_list<LuaFunctionReg> funcs);
        void           register_global(std::initializer_list<LuaFunctionReg> funcs);
        void           ensure_table(const std::string& dotted_path);
        void           push_callback(LuaNativeFn fn) const;
        void           push_callback(LuaNativeFnWithContext fn, void* userdata) const;

        bool           exec_file(const std::string& path);

        LuaRegistryRef ref_value(int index) const;
        LuaRegistryRef ref_function(int index) const;
        bool           push_ref(const LuaRegistryRef& ref) const;
        bool           pcall(int nargs, int nresults, const char* context) const;
        bool           call_ref(const LuaRegistryRef& ref, int nargs, int nresults, const char* context) const;
        bool           call_ref_string(const LuaRegistryRef& ref, std::string& out, const char* context) const;
        bool           call_ref_with_int_fields(const LuaRegistryRef& ref,
            std::initializer_list<std::pair<const char*, int64_t> > fields,
            const char* context) const;
};