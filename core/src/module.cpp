#include <module.hpp>
#include <runtime.hpp>

RuntimeState Module::runtime_state() const {
    return deps_.runtime.state();
}
