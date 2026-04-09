#include <core_config.hpp>
#include <log.hpp>
#include <runtime_store.hpp>
#include <string_utils.hpp>

#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

bool to_int32_checked(const RuntimeValue& value, int& out) {
    const auto* iv = value.as_int();
    if (!iv)
        return false;
    if (*iv < std::numeric_limits<int>::min() || *iv > std::numeric_limits<int>::max())
        return false;
    out = (int)*iv;
    return true;
}

std::optional<std::string> check_known_fields(
    const RuntimeValue::Object& obj,
    std::initializer_list<const char*> allowed,
    const std::string& origin) {
    std::unordered_set<std::string> known;
    known.reserve(allowed.size());
    for (const char* k : allowed)
        known.emplace(k);

    for (const auto& [key, _] : obj) {
        if (!known.count(key))
            return origin + ": unknown field '" + key + "'";
    }
    return std::nullopt;
}

std::optional<std::string> parse_monitors_runtime(
    const RuntimeValue& value,
    std::vector<MonitorAlias>& out) {
    const auto* arr = value.as_array();
    if (!arr)
        return std::string("must be an array");

    for (size_t i = 0; i < arr->size(); i++) {
        const auto*       obj    = (*arr)[i].as_object();
        const std::string origin = "monitors[" + std::to_string(i + 1) + "]";
        if (!obj)
            return origin + ": entry must be an object";

        if (auto e = check_known_fields(*obj,
        { "name", "output", "width", "height", "refresh", "rate",
          "rotation", "enabled" },
            origin);
            e.has_value()) {
            return e;
        }

        MonitorAlias alias;

        auto         read_string_required = [&](const char* key, std::string& out_s) -> std::optional<std::string> {
                auto it = obj->find(key);
                if (it == obj->end())
                    return origin + ": '" + key + "' is required";
                const auto* sv = it->second.as_string();
                if (!sv)
                    return origin + ": '" + key + "' must be a string";
                out_s = *sv;
                return std::nullopt;
            };
        auto read_int_required = [&](const char* key, int& out_i) -> std::optional<std::string> {
                auto it = obj->find(key);
                if (it == obj->end())
                    return origin + ": '" + key + "' is required";
                if (!to_int32_checked(it->second, out_i))
                    return origin + ": '" + key + "' must be an integer";
                return std::nullopt;
            };
        auto read_bool_required = [&](const char* key, bool& out_b) -> std::optional<std::string> {
                auto it = obj->find(key);
                if (it == obj->end())
                    return origin + ": '" + key + "' is required";
                const auto* bv = it->second.as_bool();
                if (!bv)
                    return origin + ": '" + key + "' must be a boolean";
                out_b = *bv;
                return std::nullopt;
            };

        if (auto e = read_string_required("name", alias.alias); e.has_value()) return e;
        if (auto e = read_string_required("output", alias.output); e.has_value()) return e;
        if (auto e = read_int_required("width", alias.width); e.has_value()) return e;
        if (auto e = read_int_required("height", alias.height); e.has_value()) return e;
        if (auto e = read_string_required("rotation", alias.rotation); e.has_value()) return e;
        if (auto e = read_bool_required("enabled", alias.enabled); e.has_value()) return e;

        auto rr = obj->find("refresh");
        if (rr != obj->end()) {
            if (!to_int32_checked(rr->second, alias.rate))
                return origin + ": 'refresh' must be an integer";
        }
        auto rate = obj->find("rate");
        if (rate != obj->end()) {
            if (!to_int32_checked(rate->second, alias.rate))
                return origin + ": 'rate' must be an integer";
        }

        if (alias.width <= 0 || alias.height <= 0)
            return origin + ": invalid size " + std::to_string(alias.width) + "x" + std::to_string(alias.height);
        if (!is_valid_rotation(alias.rotation))
            return origin + ": invalid rotation '" + alias.rotation + "'";

        out.push_back(std::move(alias));
    }

    return std::nullopt;
}

std::optional<std::string> parse_compose_runtime(
    const RuntimeValue& value,
    MonitorCompose& compose_out) {
    const auto* obj = value.as_object();
    if (!obj)
        return std::string("must be an object");

    if (auto e = check_known_fields(*obj, { "primary", "layout" }, "compose_monitors"); e.has_value())
        return e;

    auto pit = obj->find("primary");
    if (pit == obj->end())
        return std::string("compose_monitors: 'primary' is required");
    const auto* primary = pit->second.as_string();
    if (!primary)
        return std::string("compose_monitors: 'primary' must be a string");

    auto lit = obj->find("layout");
    if (lit == obj->end())
        return std::string("compose_monitors: 'layout' is required");
    const auto* layout = lit->second.as_array();
    if (!layout)
        return std::string("compose_monitors: 'layout' must be an array");

    MonitorCompose compose;
    compose.defined = true;
    compose.primary = *primary;

    for (size_t i = 0; i < layout->size(); i++) {
        const auto*       link_obj = (*layout)[i].as_object();
        const std::string origin   = "compose_monitors.layout[" + std::to_string(i + 1) + "]";
        if (!link_obj)
            return origin + ": entry must be an object";

        if (auto e = check_known_fields(*link_obj,
            { "monitor", "relative_to", "anchor", "side", "shift" },
            origin);
            e.has_value()) {
            return e;
        }

        MonitorComposeLink link;

        auto               mit = link_obj->find("monitor");
        if (mit == link_obj->end())
            return origin + ": 'monitor' is required";
        const auto* monitor = mit->second.as_string();
        if (!monitor)
            return origin + ": 'monitor' must be a string";
        link.monitor = *monitor;

        auto rit = link_obj->find("relative_to");
        if (rit != link_obj->end()) {
            const auto* rel = rit->second.as_string();
            if (!rel)
                return origin + ": 'relative_to' must be a string";
            link.relative_to = *rel;
        }

        auto ait = link_obj->find("anchor");
        if (ait != link_obj->end()) {
            const auto* anchor = ait->second.as_string();
            if (!anchor)
                return origin + ": 'anchor' must be a string";
            link.relative_to = *anchor;
        }

        auto sit = link_obj->find("side");
        if (sit != link_obj->end()) {
            const auto* side = sit->second.as_string();
            if (!side)
                return origin + ": 'side' must be a string";
            link.side = lower_ascii(*side);
        }

        auto shift_it = link_obj->find("shift");
        if (shift_it != link_obj->end()) {
            if (!to_int32_checked(shift_it->second, link.shift))
                return origin + ": 'shift' must be an integer";
        }

        compose.layout.push_back(std::move(link));
    }

    compose_out = std::move(compose);
    return std::nullopt;
}

std::optional<std::string> parse_workspaces_runtime(
    const RuntimeValue& value,
    std::vector<WorkspaceDef>& out) {
    const auto* arr = value.as_array();
    if (!arr)
        return std::string("must be an array");

    for (size_t i = 0; i < arr->size(); i++) {
        const auto*       obj    = (*arr)[i].as_object();
        const std::string origin = "workspaces[" + std::to_string(i + 1) + "]";
        if (!obj)
            return origin + ": entry must be an object";

        if (auto e = check_known_fields(*obj, { "name", "monitor" }, origin); e.has_value())
            return e;

        auto nit = obj->find("name");
        if (nit == obj->end())
            return origin + ": 'name' is required";
        const auto* name = nit->second.as_string();
        if (!name || name->empty())
            return origin + ": 'name' must be a non-empty string";

        WorkspaceDef def;
        def.name = *name;

        auto mit = obj->find("monitor");
        if (mit != obj->end()) {
            const auto* mon = mit->second.as_string();
            if (!mon)
                return origin + ": 'monitor' must be a string";
            def.monitor = *mon;
        }

        out.push_back(std::move(def));
    }

    return std::nullopt;
}

} // namespace

namespace config_runtime {

// ---------------------------------------------------------------------------
// Theme parser (RuntimeValue-based, replaces ad-hoc Lua table read)
// ---------------------------------------------------------------------------

std::optional<std::string> parse_theme_runtime(
    const RuntimeValue& value,
    ThemeConfig& out) {
    const auto* obj = value.as_object();
    if (!obj)
        return std::string("must be an object");

    auto read_int = [&](const char* key, int& v) {
            auto it = obj->find(key);
            if (it != obj->end()) {
                if (const auto* iv = it->second.as_int())
                    v = static_cast<int>(*iv);
                else if (const auto* nv = it->second.as_num())
                    v = static_cast<int>(*nv);
            }
        };
    auto read_str = [&](const char* key, std::string& v) {
            auto it = obj->find(key);
            if (it != obj->end()) {
                if (const auto* sv = it->second.as_string())
                    v = *sv;
            }
        };

    read_int("dpi",          out.dpi);
    read_int("cursor_size",  out.cursor_size);
    read_str("cursor_theme", out.cursor_theme);
    read_str("font",   out.font);
    read_str("bg",     out.bg);
    read_str("fg",     out.fg);
    read_str("alt_bg", out.alt_bg);
    read_str("alt_fg", out.alt_fg);
    read_str("accent", out.accent);
    read_int("gap",    out.gap);

    auto bit = obj->find("border");
    if (bit != obj->end()) {
        const auto* bobj = bit->second.as_object();
        if (bobj) {
            auto bri = [&](const char* key, int& v) {
                    auto it2 = bobj->find(key);
                    if (it2 != bobj->end()) {
                        if (const auto* iv = it2->second.as_int())
                            v = static_cast<int>(*iv);
                        else if (const auto* nv = it2->second.as_num())
                            v = static_cast<int>(*nv);
                    }
                };
            auto brs = [&](const char* key, std::string& v) {
                    auto it2 = bobj->find(key);
                    if (it2 != bobj->end()) {
                        if (const auto* sv = it2->second.as_string())
                            v = *sv;
                    }
                };
            bri("thickness", out.border_thickness);
            brs("focused",   out.border_focused);
            brs("unfocused", out.border_unfocused);
        }
    }

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// CoreConfig registration into RuntimeStore
// ---------------------------------------------------------------------------

void register_core_config(CoreConfig& cc, RuntimeStore& store) {
    cc.monitors.set_parse(
        [](const RuntimeValue& v, std::vector<MonitorAlias>& out) {
            return parse_monitors_runtime(v, out);
        },
        RuntimeValueType::Array);
    store.register_setting("monitors", cc.monitors);

    cc.compose.set_parse(
        [](const RuntimeValue& v, MonitorCompose& out) {
            return parse_compose_runtime(v, out);
        },
        RuntimeValueType::Object);
    store.register_setting("compose_monitors", cc.compose);

    cc.workspaces.set_parse(
        [](const RuntimeValue& v, std::vector<WorkspaceDef>& out) {
            return parse_workspaces_runtime(v, out);
        },
        RuntimeValueType::Array);
    store.register_setting("workspaces", cc.workspaces);

    cc.theme.set_parse(
        [](const RuntimeValue& v, ThemeConfig& out) {
            return parse_theme_runtime(v, out);
        },
        RuntimeValueType::Object);
    store.register_setting("theme", cc.theme);
}

} // namespace config_runtime
