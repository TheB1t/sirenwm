#include <wl_ports.hpp>

namespace backend {

std::string WlKeyboardPort::current_layout() const {
    if (group_ < layouts_.size())
        return layouts_[group_];
    return "us";
}

std::vector<std::string> WlKeyboardPort::layout_names() const {
    return layouts_;
}

void WlKeyboardPort::apply(const std::vector<std::string>& layouts, const std::string&) {
    if (!layouts.empty())
        layouts_ = layouts;
    group_ = 0;
}

void WlKeyboardPort::restore() {
    layouts_ = {"us"};
    group_ = 0;
}

uint32_t WlKeyboardPort::get_group() const {
    return group_;
}

void WlKeyboardPort::set_group(uint32_t group) {
    if (group < layouts_.size())
        group_ = group;
}

} // namespace backend
