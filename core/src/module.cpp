#include <module.hpp>
#include <runtime.hpp>

RuntimeState Module::runtime_state() const {
    return deps_.runtime.state();
}

RuntimeStore& Module::store() {
    return deps_.runtime.store();
}

LuaHost& Module::lua() {
    return deps_.runtime.lua();
}
