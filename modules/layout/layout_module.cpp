#include "layout_module.hpp"

#include <core.hpp>
#include <config.hpp>
#include <layout.hpp>
#include <module_registry.hpp>

#include <string>

namespace {

bool apply_layout_assignment(Core& core, LuaContext& lua, int table_idx, std::string& err) {
    table_idx = lua.abs_index(table_idx);
    if (!lua.is_table(table_idx)) {
        err = "siren.layout: expected table";
        return false;
    }

    lua.get_field(table_idx, "name");
    if (!lua.is_nil(-1)) {
        if (!lua.is_string(-1)) {
            lua.pop();
            err = "siren.layout.name: must be a string";
            return false;
        }
        (void)core.dispatch(command::SetLayout{ lua.to_string(-1) });
    }
    lua.pop();

    lua.get_field(table_idx, "master_factor");
    if (!lua.is_nil(-1)) {
        if (!lua.is_number(-1)) {
            lua.pop();
            err = "siren.layout.master_factor: must be a number";
            return false;
        }
        (void)core.dispatch(command::SetMasterFactor{ (float)lua.to_number(-1) });
    }
    lua.pop();

    return true;
}

} // namespace

void LayoutModule::on_init(Core& core) {
    core.register_layout("tile",    layout::tile);
    core.register_layout("monocle", layout::monocle);
    config().register_lua_assignment_handler("layout",
        [&core](LuaContext& lua, int table_idx, std::string& err) -> bool {
            return apply_layout_assignment(core, lua, table_idx, err);
        });
}

void LayoutModule::on_lua_init(Core&) {}

static bool _swm_registered_lua_symbols_layout = []() {
        module_registry_static::add_lua_symbol_registration("layout", "layout");
        return true;
    }();

SWM_REGISTER_MODULE("layout", LayoutModule)