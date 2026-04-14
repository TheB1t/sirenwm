#pragma once

#include <swm/ipc/backend_protocol.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

#include <sys/types.h>

namespace swm::ipc {

class Channel {
public:
    Channel() noexcept = default;
    explicit Channel(int fd) noexcept : fd_(fd) {}
    ~Channel();

    Channel(const Channel&)            = delete;
    Channel& operator=(const Channel&) = delete;

    Channel(Channel&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)) {}

    Channel& operator=(Channel&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    static Channel connect_seqpacket(const std::string& path);
    static Channel listen_seqpacket(const std::string& path, int backlog = 4);

    Channel accept() const;

    explicit operator bool() const noexcept { return fd_ >= 0; }
    int fd() const noexcept { return fd_; }
    int release() noexcept {
        int released = fd_;
        fd_ = -1;
        return released;
    }

    void close() noexcept;
    bool set_nonblocking(bool enabled) const;

    ssize_t send_bytes(const void* data, std::size_t size, int send_fd = -1) const;
    ssize_t receive_bytes(void* data, std::size_t capacity, int* received_fd = nullptr) const;

    template <typename Payload>
    bool send_message(const Payload& payload, int send_fd = -1) const {
        auto envelope = make_message(payload);
        std::array<std::byte, wire_size<Payload>()> buffer {};
        std::memcpy(buffer.data(), &envelope.header, sizeof(envelope.header));
        std::memcpy(buffer.data() + sizeof(envelope.header), &envelope.payload, sizeof(envelope.payload));
        return send_bytes(buffer.data(), buffer.size(), send_fd) ==
               static_cast<ssize_t>(buffer.size());
    }

    template <typename Payload>
    bool receive_message(Envelope<Payload>& envelope, int* received_fd = nullptr) const {
        std::array<std::byte, wire_size<Payload>()> buffer {};
        auto rc = receive_bytes(buffer.data(), buffer.size(), received_fd);
        if (rc != static_cast<ssize_t>(buffer.size()))
            return false;
        std::memcpy(&envelope.header, buffer.data(), sizeof(envelope.header));
        std::memcpy(&envelope.payload, buffer.data() + sizeof(envelope.header), sizeof(envelope.payload));
        return envelope.header.magic == kBackendProtocolMagic &&
               envelope.header.version == kBackendProtocolVersion &&
               envelope.header.type == MessageTraits<Payload>::type &&
               envelope.header.size == sizeof(Payload);
    }

private:
    int fd_ = -1;
};

} // namespace swm::ipc
