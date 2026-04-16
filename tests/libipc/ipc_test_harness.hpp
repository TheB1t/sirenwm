#pragma once
// Test fixtures for libipc unit tests.
//
//   ChannelPair  — socketpair(SEQPACKET) wrapped as two Channel endpoints.
//   FakePeer     — thin wrapper over a Channel for send/receive-typed in tests.
//   EnvGuard     — scoped setenv/unsetenv for endpoint-resolution tests.

#include <swm/ipc/backend_protocol.hpp>
#include <swm/ipc/channel.hpp>

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include <sys/socket.h>
#include <unistd.h>

namespace swm::ipc::test {

struct ChannelPair {
    Channel a;
    Channel b;

    static ChannelPair make() {
        int fds[2] = { -1, -1 };
        if (::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) != 0)
            return {};
        return ChannelPair{ Channel{ fds[0] }, Channel{ fds[1] } };
    }
};

class FakePeer {
    public:
        explicit FakePeer(Channel ch) : ch_(std::move(ch)) {}

        Channel& channel() { return ch_; }
        const Channel& channel() const { return ch_; }

        template <typename Payload>
        bool send(const Payload& payload, int send_fd = -1) {
            return ch_.send_message(payload, send_fd);
        }

        template <typename Payload>
        std::optional<Payload> receive(int* received_fd = nullptr) {
            Envelope<Payload> env;
            if (!ch_.receive_message(env, received_fd))
                return std::nullopt;
            return env.payload;
        }

    private:
        Channel ch_;
};

class EnvGuard {
    public:
        EnvGuard(const char* name, const char* value) : name_(name) {
            if (const char* prev = std::getenv(name)) {
                had_prev_ = true;
                prev_     = prev;
            }
            if (value)
                ::setenv(name, value, 1);
            else
                ::unsetenv(name);
        }

        ~EnvGuard() {
            if (had_prev_)
                ::setenv(name_.c_str(), prev_.c_str(), 1);
            else
                ::unsetenv(name_.c_str());
        }

        EnvGuard(const EnvGuard&)            = delete;
        EnvGuard& operator=(const EnvGuard&) = delete;

    private:
        std::string name_;
        std::string prev_;
        bool        had_prev_ = false;
};

} // namespace swm::ipc::test
