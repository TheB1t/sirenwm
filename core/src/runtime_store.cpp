#include <runtime_store.hpp>

#include <log.hpp>
#include <lua_host.hpp>  // LuaContext

// ---------------------------------------------------------------------------
// RuntimeStore
// ---------------------------------------------------------------------------

void RuntimeStore::register_setting(const std::string& key, Setting& setting) {
    settings_[key] = &setting;
}

Setting* RuntimeStore::find(const std::string& key) {
    auto it = settings_.find(key);
    return it != settings_.end() ? it->second : nullptr;
}

const Setting* RuntimeStore::find(const std::string& key) const {
    auto it = settings_.find(key);
    return it != settings_.end() ? it->second : nullptr;
}

std::vector<std::string> RuntimeStore::keys() const {
    std::vector<std::string> out;
    out.reserve(settings_.size());
    for (const auto& [k, _] : settings_)
        out.push_back(k);
    return out;
}

void RuntimeStore::snapshot_all() {
    for (auto& [_, s] : settings_)
        s->snapshot();
}

void RuntimeStore::rollback_all() {
    for (auto& [_, s] : settings_)
        s->rollback();
}

void RuntimeStore::commit_all() {
    for (auto& [_, s] : settings_)
        s->commit();
}

void RuntimeStore::clear_all() {
    for (auto& [_, s] : settings_)
        s->clear();
}

bool RuntimeStore::apply_from_lua(LuaContext& ctx, int siren_idx) {
    bool ok = true;
    for (const auto& [key, setting] : settings_) {
        if (setting->expected_type() == RuntimeValueType::Null)
            continue;  // imperative setting, not post-exec

        ctx.get_field(siren_idx, key.c_str());
        if (ctx.is_nil(-1) || ctx.is_function(-1)) {
            ctx.pop();
            continue;
        }

        RuntimeValue rv;
        if (!runtime_value_from_lua(ctx, -1, rv)) {
            LOG_ERR("RuntimeStore: setting '%s' has unsupported Lua value type",
                key.c_str());
            ctx.pop();
            ok = false;
            continue;
        }

        auto err = setting->apply_runtime_value(rv);
        if (err.has_value()) {
            LOG_ERR("RuntimeStore: setting '%s' is invalid: %s",
                key.c_str(), err->c_str());
            ok = false;
        }
        ctx.pop();
    }
    return ok;
}

std::vector<std::string> RuntimeStore::validate_all() const {
    std::vector<std::string> errs;
    for (const auto& [_, s] : settings_) {
        auto e = s->validate();
        errs.insert(errs.end(), e.begin(), e.end());
    }
    return errs;
}
