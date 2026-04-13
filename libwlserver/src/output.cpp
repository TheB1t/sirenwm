#include <wl/server/output.hpp>

extern "C" {
#include <wayland-server-protocol.h>
}

namespace wl::server {

const wl_interface* Output::interface() { return &wl_output_interface; }

Output::Output(Display& display, int32_t width, int32_t height, int32_t refresh_mhz,
               const std::string& make, const std::string& model)
    : global_(display, this)
    , width_(width), height_(height), refresh_(refresh_mhz)
    , make_(make), model_(model) {}

void Output::bind(wl_client* client, uint32_t version, uint32_t id) {
    auto* resource = wl_resource_create(client, &wl_output_interface,
                                        static_cast<int>(version), id);
    if (!resource) { wl_client_post_no_memory(client); return; }

    static const struct wl_output_interface vtable = {
        .release = [](wl_client*, wl_resource* r) { wl_resource_destroy(r); },
    };
    wl_resource_set_implementation(resource, &vtable, nullptr, nullptr);
    send_info(resource);
}

void Output::send_info(wl_resource* resource) {
    wl_output_send_geometry(resource, 0, 0, 0, 0,
        WL_OUTPUT_SUBPIXEL_UNKNOWN,
        make_.c_str(), model_.c_str(),
        WL_OUTPUT_TRANSFORM_NORMAL);

    wl_output_send_mode(resource,
        WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
        width_, height_, refresh_);

    if (wl_resource_get_version(resource) >= 2)
        wl_output_send_done(resource);
    if (wl_resource_get_version(resource) >= 3)
        wl_output_send_scale(resource, 1);
    if (wl_resource_get_version(resource) >= 4) {
        if (!name_.empty())
            wl_output_send_name(resource, name_.c_str());
        if (!description_.empty())
            wl_output_send_description(resource, description_.c_str());
    }
}

void Output::set_size(int32_t width, int32_t height) {
    width_  = width;
    height_ = height;
}

void Output::set_name(const std::string& name) { name_ = name; }
void Output::set_description(const std::string& desc) { description_ = desc; }

} // namespace wl::server
