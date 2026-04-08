// Stubs for backend free functions that live in the real backend library.
// Module tests need these symbols but don't link the X11 backend.

#include <backend/input_port.hpp>
#include <string>

namespace backend {

uint32_t keysym_from_name(const std::string& name) {
    // Minimal stub: map a few common names for test purposes.
    if (name == "Return")    return 0xff0d;
    if (name == "Escape")    return 0xff1b;
    if (name == "space")     return 0x0020;
    if (name == "Tab")       return 0xff09;
    if (name == "BackSpace") return 0xff08;
    // Single ASCII character
    if (name.size() == 1 && name[0] >= 0x20 && name[0] <= 0x7e)
        return (uint32_t)name[0];
    // Digit keys
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9')
        return (uint32_t)name[0];
    // Function keys F1..F12
    if (name.size() >= 2 && name[0] == 'F') {
        int n = std::stoi(name.substr(1));
        if (n >= 1 && n <= 12)
            return 0xffbe + (uint32_t)(n - 1);
    }
    return 0;
}

} // namespace backend
