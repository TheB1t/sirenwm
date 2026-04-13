#pragma once

// Convenience header for common/generic libwl wrappers.
// This header intentionally excludes server-runtime APIs.

// Core
#include <wl/display.hpp>
#include <wl/event_loop.hpp>
#include <wl/global.hpp>
#include <wl/listener.hpp>
#include <wl/protocol.hpp>
#include <wl/shm_buffer.hpp>

// Client-side
#include <wl/client_display.hpp>
#include <wl/proxy.hpp>
#include <wl/registry.hpp>
