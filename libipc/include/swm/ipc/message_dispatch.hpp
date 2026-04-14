#pragma once

#include <swm/ipc/backend_protocol.hpp>

#include <concepts>
#include <cstring>
#include <utility>

namespace swm::ipc {

template <typename Payload>
bool read_message_payload(const MessageHeader& header, const void* payload_bytes, Payload& out) {
    if (header.type != MessageTraits<Payload>::type)
        return false;
    if (header.size != sizeof(Payload))
        return false;
    std::memcpy(&out, payload_bytes, sizeof(Payload));
    return true;
}

namespace detail {

template <typename Handler, typename Payload>
concept HandlesWithFdBool =
    requires(Handler&& handler, const Payload& payload, int fd) {
        { std::forward<Handler>(handler).on(payload, fd) } -> std::convertible_to<bool>;
    };

template <typename Handler, typename Payload>
concept HandlesWithFdVoid =
    requires(Handler&& handler, const Payload& payload, int fd) {
        { std::forward<Handler>(handler).on(payload, fd) } -> std::same_as<void>;
    };

template <typename Handler, typename Payload>
concept HandlesBool =
    requires(Handler&& handler, const Payload& payload) {
        { std::forward<Handler>(handler).on(payload) } -> std::convertible_to<bool>;
    };

template <typename Handler, typename Payload>
concept HandlesVoid =
    requires(Handler&& handler, const Payload& payload) {
        { std::forward<Handler>(handler).on(payload) } -> std::same_as<void>;
    };

template <typename Handler, typename Payload>
bool invoke_handler(Handler&& handler, const Payload& payload, int received_fd) {
    if constexpr (HandlesWithFdBool<Handler, Payload>) {
        return static_cast<bool>(std::forward<Handler>(handler).on(payload, received_fd));
    } else if constexpr (HandlesWithFdVoid<Handler, Payload>) {
        std::forward<Handler>(handler).on(payload, received_fd);
        return true;
    } else if constexpr (HandlesBool<Handler, Payload>) {
        return static_cast<bool>(std::forward<Handler>(handler).on(payload));
    } else if constexpr (HandlesVoid<Handler, Payload>) {
        std::forward<Handler>(handler).on(payload);
        return true;
    } else {
        return false;
    }
}

} // namespace detail

template <typename Handler>
bool dispatch_backend_message(const MessageHeader& header, const void* payload_bytes,
    int received_fd, Handler&& handler) {
    switch (header.type) {
#define SWM_IPC_DISPATCH_CASE(TypeName, MessageKind) \
    case BackendMessageType::MessageKind: { \
        TypeName payload {}; \
        if (!read_message_payload(header, payload_bytes, payload)) \
            return false; \
        return detail::invoke_handler(std::forward<Handler>(handler), payload, received_fd); \
    }

    SWM_IPC_BACKEND_MESSAGES(SWM_IPC_DISPATCH_CASE)

#undef SWM_IPC_DISPATCH_CASE
    default:
        return false;
    }
}

} // namespace swm::ipc
