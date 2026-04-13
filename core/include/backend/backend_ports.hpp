#pragma once

// Aggregate of backend capability ports exposed by Backend.
//
// Runtime and modules access backend capabilities through Backend::ports()
// without depending on concrete backend types.
//
// All backends are required to provide input/monitor/render/keyboard, so
// those fields are references. gl is optional (only available in debug
// builds) and therefore stays a pointer.

namespace backend {
class InputPort;
class MonitorPort;
class RenderPort;
class KeyboardPort;
class GLPort;
class TrayHostPort;

struct BackendPorts {
    InputPort&    input;
    MonitorPort&  monitor;
    RenderPort&   render;
    KeyboardPort& keyboard;
    GLPort*       gl        = nullptr;
    TrayHostPort* tray_host = nullptr;
};

} // namespace backend
