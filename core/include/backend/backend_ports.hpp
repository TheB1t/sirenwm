#pragma once

// Aggregate of backend capability ports passed from Backend to Core.
//
// Core holds a value copy after Runtime::start() and exposes it via
// Core::ports(). Modules reach every backend capability through
// core().ports().xxx — the concrete Backend class is never visible from
// module code.
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
