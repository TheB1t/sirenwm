#include <display_server_ports.hpp>

namespace backend {

std::string DisplayServerKeyboardPort::current_layout() const {
    if (group_ < layouts_.size())
        return layouts_[group_];
    return "us";
}

std::vector<std::string> DisplayServerKeyboardPort::layout_names() const {
    return layouts_;
}

void DisplayServerKeyboardPort::apply(const std::vector<std::string>& layouts, const std::string&) {
    if (!layouts.empty())
        layouts_ = layouts;
    group_ = 0;
}

void DisplayServerKeyboardPort::restore() {
    layouts_ = {"us"};
    group_ = 0;
}

uint32_t DisplayServerKeyboardPort::get_group() const {
    return group_;
}

void DisplayServerKeyboardPort::set_group(uint32_t group) {
    if (group < layouts_.size())
        group_ = group;
}

} // namespace backend
