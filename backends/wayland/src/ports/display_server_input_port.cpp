#include <display_server_ports.hpp>
#include <display_server_backend.hpp>

extern "C" {
#include <xkbcommon/xkbcommon.h>
}

namespace backend {

void DisplayServerInputPort::grab_key(uint32_t keysym, uint16_t mods) {
    intercepts_.push_back({keysym, static_cast<uint32_t>(mods)});
    send_intercepts();
}

void DisplayServerInputPort::ungrab_all_keys() {
    intercepts_.clear();
    send_intercepts();
}

void DisplayServerInputPort::send_intercepts() {
    backend_.set_keyboard_intercepts(
        intercepts_.data(), intercepts_.size() * sizeof(KeyIntercept));
}

void DisplayServerInputPort::grab_button(WindowId, uint8_t, uint16_t) {}
void DisplayServerInputPort::ungrab_all_buttons(WindowId) {}
void DisplayServerInputPort::grab_button_any(WindowId) {}

void DisplayServerInputPort::grab_pointer() {
    backend_.grab_pointer();
}

void DisplayServerInputPort::ungrab_pointer() {
    backend_.ungrab_pointer();
}

void DisplayServerInputPort::allow_events(bool) {}

void DisplayServerInputPort::warp_pointer(WindowId, Vec2i16 pos) {
    backend_.warp_pointer(pos.x(), pos.y());
}

void DisplayServerInputPort::warp_pointer_abs(Vec2i16 pos) {
    backend_.warp_pointer(pos.x(), pos.y());
}

void DisplayServerInputPort::flush() {
}

uint32_t keysym_from_name(const std::string& name) {
    return xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_NO_FLAGS);
}

} // namespace backend
