#include "monitor.hpp"
#include <backend/backend.hpp>
#include <backend/monitor_port.hpp>
#include <core.hpp>
#include <config.hpp>
#include <module_registry.hpp>
#include <log.hpp>
#include <runtime.hpp>
#include <algorithm>
#include <unordered_map>
#include <limits>

using PositionMap = std::unordered_map<std::string, std::pair<int, int> >;

static bool is_rotated(const std::string& rotation) {
    return rotation == "left" || rotation == "right";
}

static std::pair<int,int> rotated_size(const MonitorAlias& a) {
    int w = is_rotated(a.rotation) ? a.height : a.width;
    int h = is_rotated(a.rotation) ? a.width  : a.height;
    return { w, h };
}

// Resolves monitor alias -> absolute position from compose graph.
// Implemented as a struct to allow natural recursion without std::function overhead.
struct ComposeResolver {
    const std::vector<MonitorAlias>&                           aliases;
    const MonitorCompose&                                      compose;
    PositionMap&                                               pos;
    std::unordered_map<std::string, const MonitorAlias*>       by_alias;
    std::unordered_map<std::string, const MonitorComposeLink*> by_monitor;
    std::unordered_map<std::string, int>                       state;  // 0=unknown,1=visiting,2=done

    ComposeResolver(const std::vector<MonitorAlias>& aliases,
        const MonitorCompose& compose,
        PositionMap& pos)
        : aliases(aliases), compose(compose), pos(pos) {
        for (auto& a : aliases)
            by_alias[a.alias] = &a;
        for (auto& l : compose.layout)
            by_monitor[l.monitor] = &l;
    }

    bool resolve(const std::string& name) {
        if (state[name] == 2) return true;
        if (state[name] == 1) {
            LOG_ERR("monitors: compose cycle at '%s'", name.c_str());
            return false;
        }

        auto ai = by_alias.find(name);
        if (ai == by_alias.end()) {
            LOG_ERR("monitors: compose references unknown monitor alias '%s'", name.c_str());
            return false;
        }
        const auto* self             = ai->second;

        auto        it               = by_monitor.find(name);
        bool        implicit_primary = (name == compose.primary && it == by_monitor.end());
        if (it == by_monitor.end() && !implicit_primary) {
            LOG_ERR("monitors: compose entry for monitor '%s' is missing", name.c_str());
            return false;
        }

        state[name] = 1;
        if (implicit_primary || name == compose.primary) {
            pos[name]   = { 0, 0 };
            state[name] = 2;
            return true;
        }

        const auto& link = *it->second;
        if (link.relative_to.empty()) {
            LOG_ERR("monitors: monitor '%s' must set relative_to", name.c_str());
            state[name] = 2;
            return false;
        }
        if (!resolve(link.relative_to)) {
            state[name] = 2;
            return false;
        }

        auto ri = by_alias.find(link.relative_to);
        if (ri == by_alias.end()) {
            LOG_ERR("monitors: monitor '%s' references unknown relative_to '%s'",
                name.c_str(), link.relative_to.c_str());
            state[name] = 2;
            return false;
        }
        auto rp = pos.find(link.relative_to);
        if (rp == pos.end()) {
            LOG_ERR("monitors: monitor '%s' relative '%s' has no resolved position",
                name.c_str(), link.relative_to.c_str());
            state[name] = 2;
            return false;
        }

        auto [self_w, self_h] = rotated_size(*self);
        auto [rel_w,  rel_h]  = rotated_size(*ri->second);
        int rel_x = rp->second.first;
        int rel_y = rp->second.second;
        int x     = rel_x;
        int y     = rel_y;

        if      (link.side == "left") {
            x = rel_x - self_w;      y = rel_y + link.shift;
        } else if (link.side ==
            "right") {
            x = rel_x + rel_w;       y = rel_y + link.shift;
        } else if (link.side ==
            "top") {
            x = rel_x + link.shift;   y = rel_y - self_h;
        } else if (link.side ==
            "bottom") {
            x = rel_x + link.shift;   y = rel_y + rel_h;
        } else {
            LOG_ERR("monitors: monitor '%s' has invalid side '%s'", name.c_str(), link.side.c_str());
            state[name] = 2;
            return false;
        }

        pos[name]   = { x, y };
        state[name] = 2;
        return true;
    }
};

// Resolve monitor alias -> absolute position from compose graph.
// Invalid compose entries are logged and skipped.
static PositionMap compose_positions(const std::vector<MonitorAlias>& aliases,
    const MonitorCompose& compose) {
    PositionMap     pos;
    ComposeResolver resolver(aliases, compose, pos);

    if (!compose.primary.empty())
        resolver.resolve(compose.primary);

    for (auto& alias : aliases) {
        if (!alias.enabled)
            continue;
        resolver.resolve(alias.alias);
    }

    // Normalize to non-negative coordinates (CRTC positions must be >= 0).
    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    for (auto& alias : aliases) {
        if (!alias.enabled)
            continue;
        auto it = pos.find(alias.alias);
        if (it == pos.end())
            continue;
        min_x = std::min(min_x, it->second.first);
        min_y = std::min(min_y, it->second.second);
    }
    if (min_x == std::numeric_limits<int>::max() || min_y == std::numeric_limits<int>::max())
        return pos;

    int shift_x = (min_x < 0) ? -min_x : 0;
    int shift_y = (min_y < 0) ? -min_y : 0;
    if (shift_x != 0 || shift_y != 0) {
        for (auto& [_, p] : pos) {
            p.first  += shift_x;
            p.second += shift_y;
        }
    }

    return pos;
}

// Build MonitorLayout vector from config aliases and compose graph.
static std::vector<backend::MonitorLayout> build_monitor_layout(const Config& config) {
    const auto& aliases = config.get_monitor_aliases();
    if (aliases.empty())
        return {};

    const auto& compose   = config.get_monitor_compose();
    auto        positions = compose_positions(aliases, compose);

    std::vector<backend::MonitorLayout> result;
    for (auto& alias : aliases) {
        backend::MonitorLayout ml;
        ml.output       = alias.output;
        ml.alias        = alias.alias;
        ml.enabled      = alias.enabled;
        ml.width        = alias.width;
        ml.height       = alias.height;
        ml.refresh_rate = alias.rate;
        ml.rotation     = alias.rotation;
        ml.primary      = (alias.alias == compose.primary);

        auto pos_it = positions.find(alias.alias);
        if (pos_it != positions.end()) {
            ml.x = pos_it->second.first;
            ml.y = pos_it->second.second;
        }

        result.push_back(std::move(ml));
    }
    return result;
}

// ---------------------------------------------------------------------------

void MonitorModule::on_init(Core&) {}

void MonitorModule::on_start(Core& core) {
    auto* mp = runtime().backend().monitor_port();
    if (!mp) {
        LOG_ERR("monitors: backend does not provide MonitorPort");
        return;
    }

    mp->apply_monitor_layout(build_monitor_layout(config()));

    {
        auto monitors = mp->get_monitors();
        LOG_INFO("monitors: found %d monitor(s) at startup:", (int)monitors.size());
        for (auto& m : monitors)
            LOG_INFO("  %s: %dx%d+%d+%d", m.name.c_str(), m.width, m.height, m.x, m.y);
        (void)core.dispatch(command::ApplyMonitorTopology{ std::move(monitors) });
    }

    runtime().set_display_change_handler([this, &core, mp]() {
            LOG_INFO("monitors: screen change detected");
            mp->apply_monitor_layout(build_monitor_layout(config()));

            auto monitors = mp->get_monitors();
            LOG_INFO("monitors: found %d monitor(s):", (int)monitors.size());
            for (auto& m : monitors)
                LOG_INFO("  %s: %dx%d+%d+%d", m.name.c_str(), m.width, m.height, m.x, m.y);

            (void)core.dispatch(command::ApplyMonitorTopology{ std::move(monitors) });
        });

    mp->select_change_events();
}

SWM_REGISTER_MODULE("monitors", MonitorModule)