#include <wl_ports.hpp>
#include <wl_backend.hpp>

extern "C" {
#include <xkbcommon/xkbcommon.h>
}

namespace backend {

void WlInputPort::grab_key(uint32_t keysym, uint16_t mods) {
    intercepts_.push_back({keysym, static_cast<uint32_t>(mods)});
    send_intercepts();
}

void WlInputPort::ungrab_all_keys() {
    intercepts_.clear();
    send_intercepts();
}

void WlInputPort::send_intercepts() {
    backend_.set_keyboard_intercepts(
        intercepts_.data(), intercepts_.size() * sizeof(KeyIntercept));
}

void WlInputPort::grab_button(WindowId, uint8_t, uint16_t) {}
void WlInputPort::ungrab_all_buttons(WindowId) {}
void WlInputPort::grab_button_any(WindowId) {}

void WlInputPort::grab_pointer() {
    backend_.Admin::grab_pointer();
}

void WlInputPort::ungrab_pointer() {
    backend_.Admin::ungrab_pointer();
}

void WlInputPort::allow_events(bool) {}

void WlInputPort::warp_pointer(WindowId, Vec2i16 pos) {
    backend_.Admin::warp_pointer(pos.x(), pos.y());
}

void WlInputPort::warp_pointer_abs(Vec2i16 pos) {
    backend_.Admin::warp_pointer(pos.x(), pos.y());
}

void WlInputPort::flush() {
    if (backend_.display())
        backend_.display().flush();
}

uint32_t keysym_from_name(const std::string& name) {
    return xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_NO_FLAGS);
}

} // namespace backend
