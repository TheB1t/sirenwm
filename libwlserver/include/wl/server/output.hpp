#pragma once

#include <wl/display.hpp>
#include <wl/global.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace wl::server {

class Output {
public:
    Output(Display& display, int32_t width, int32_t height, int32_t refresh_mhz,
           const std::string& make = "unknown", const std::string& model = "unknown");

    Output(const Output&)            = delete;
    Output& operator=(const Output&) = delete;

    void set_size(int32_t width, int32_t height);
    void set_name(const std::string& name);
    void set_description(const std::string& desc);

    int32_t width()  const { return width_; }
    int32_t height() const { return height_; }

    static const wl_interface* interface();
    static int version() { return 4; }

    void bind(wl_client* client, uint32_t version, uint32_t id);

private:
    wl::Global<Output> global_;
    int32_t     width_;
    int32_t     height_;
    int32_t     refresh_;
    std::string make_;
    std::string model_;
    std::string name_;
    std::string description_;

    std::vector<wl_resource*> bound_resources_;

    void send_info(wl_resource* resource);
};

} // namespace wl::server
