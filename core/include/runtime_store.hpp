#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <runtime_config.hpp>

class LuaContext;

// ---------------------------------------------------------------------------
// Setting — abstract base for a single configuration value in the store.
// ---------------------------------------------------------------------------

class Setting {
public:
    virtual ~Setting() = default;

    // Transactional reload support.
    virtual void snapshot() = 0;
    virtual void rollback() = 0;
    virtual void commit()   = 0;
    virtual void clear()    = 0;

    // Post-exec declarative apply: parse a RuntimeValue extracted from siren[key].
    // Returns error string on failure, nullopt on success.
    virtual std::optional<std::string> apply_runtime_value(const RuntimeValue& rv) = 0;

    // The expected RuntimeValue type for post-exec apply.
    // Null means this setting does not participate in post-exec apply
    // (it is set imperatively during Lua execution).
    virtual RuntimeValueType expected_type() const { return RuntimeValueType::Null; }

    // Per-setting validation. Called after all settings are applied.
    virtual std::vector<std::string> validate() const { return {}; }
};

// ---------------------------------------------------------------------------
// TypedSetting<T> — concrete typed setting with snapshot/rollback.
// ---------------------------------------------------------------------------

template<typename T>
class TypedSetting : public Setting {
public:
    using ParseFn = std::function<std::optional<std::string>(const RuntimeValue&, T&)>;

    explicit TypedSetting(T default_val = {})
        : value_(std::move(default_val)), default_(value_) {}

    const T& get() const { return value_; }
    T&       get_mut()   { return value_; }
    void set(T v) { value_ = std::move(v); }

    // Transaction support.
    void snapshot() override { snapshot_ = value_; }
    void rollback() override { if (snapshot_) value_ = std::move(*snapshot_); snapshot_.reset(); }
    void commit()   override { snapshot_.reset(); }
    void clear()    override { value_ = default_; }

    // Configure a parser for RuntimeValue-based (post-exec) application.
    void set_parse(ParseFn fn, RuntimeValueType type) {
        parse_ = std::move(fn);
        type_  = type;
    }

    RuntimeValueType expected_type() const override { return type_; }

    std::optional<std::string> apply_runtime_value(const RuntimeValue& rv) override {
        if (!parse_)
            return std::string("setting does not support runtime value assignment");
        T tmp{};
        if (auto err = parse_(rv, tmp))
            return err;
        value_ = std::move(tmp);
        return std::nullopt;
    }

private:
    T                value_;
    T                default_;
    std::optional<T> snapshot_;
    ParseFn          parse_;
    RuntimeValueType type_ = RuntimeValueType::Null;
};

// ---------------------------------------------------------------------------
// RuntimeStore — registry of named settings with transactional reload.
// ---------------------------------------------------------------------------

class RuntimeStore {
public:
    // Register a setting under a key. The store does NOT own the setting;
    // the caller owns the TypedSetting<T> as a value member.
    void register_setting(const std::string& key, Setting& setting);

    // Lookup (returns nullptr if not registered).
    Setting*       find(const std::string& key);
    const Setting* find(const std::string& key) const;

    // Sorted list of registered keys.
    std::vector<std::string> keys() const;

    // Transaction: snapshot/rollback/commit/clear ALL registered settings.
    void snapshot_all();
    void rollback_all();
    void commit_all();
    void clear_all();

    // Post-exec: iterate siren[key] for each registered setting whose
    // expected_type() != Null, convert to RuntimeValue, call apply_runtime_value().
    // siren_idx must point to the siren table on the Lua stack.
    // Returns true if all settings applied successfully.
    bool apply_from_lua(LuaContext& ctx, int siren_idx);

    // Collect validation errors from all registered settings.
    std::vector<std::string> validate_all() const;

private:
    std::map<std::string, Setting*> settings_;
};
