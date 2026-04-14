#pragma once

#include <swm/ipc/backend_protocol.hpp>

namespace swm::ipc {

struct BackendEventHandler {
    virtual ~BackendEventHandler() = default;

#define SWM_IPC_DECLARE_BACKEND_EVENT_HANDLER(TypeName, MessageKind) \
    virtual void on(const TypeName&) {}

    SWM_IPC_BACKEND_EVENT_MESSAGES(SWM_IPC_DECLARE_BACKEND_EVENT_HANDLER)

#undef SWM_IPC_DECLARE_BACKEND_EVENT_HANDLER
};

struct BackendCommandHandler {
    virtual ~BackendCommandHandler() = default;

#define SWM_IPC_DECLARE_BACKEND_COMMAND_HANDLER(TypeName, MessageKind) \
    virtual void on(const TypeName&) {}

    SWM_IPC_BACKEND_COMMAND_MESSAGES(SWM_IPC_DECLARE_BACKEND_COMMAND_HANDLER)

#undef SWM_IPC_DECLARE_BACKEND_COMMAND_HANDLER
};

} // namespace swm::ipc
