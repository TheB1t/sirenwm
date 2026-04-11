#pragma once

#include <memory>

#include <backend/tray_host.hpp>

namespace backend {

// Factory port for creating TrayHost instances. Optional — backends that
// don't support system tray leave the pointer in BackendPorts as nullptr.
class TrayHostPort {
    public:
        virtual ~TrayHostPort() = default;

        virtual std::unique_ptr<TrayHost>
        create(WindowId owner_bar_window, int bar_x, int bar_y, int bar_h,
            bool own_selection) = 0;
};

} // namespace backend
