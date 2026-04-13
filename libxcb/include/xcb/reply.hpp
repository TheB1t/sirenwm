#pragma once

#include <cstdlib>
#include <memory>

namespace xcb {

// RAII wrapper for xcb reply pointers (allocated by xcb, freed with free()).
// Usage:
//   auto reply = xcb::reply(xcb_get_window_attributes_reply(conn, cookie, nullptr));
//   if (reply) reply->your_event_mask;

template<typename T>
struct ReplyDeleter {
    void operator()(T* p) const { free(p); }
};

template<typename T>
using Reply = std::unique_ptr<T, ReplyDeleter<T>>;

template<typename T>
Reply<T> reply(T* raw) {
    return Reply<T>(raw);
}

} // namespace xcb
