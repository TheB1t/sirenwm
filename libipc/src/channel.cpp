#include <swm/ipc/channel.hpp>

#include <array>
#include <cstddef>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

bool make_sockaddr(const std::string& path, sockaddr_un& addr, socklen_t& addr_len) {
    if (path.empty() || path.size() >= sizeof(addr.sun_path))
        return false;

    addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    addr_len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    return true;
}

} // namespace

namespace swm::ipc {

Channel::~Channel() {
    close();
}

Channel Channel::connect_seqpacket(const std::string& path) {
    auto fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return {};

    sockaddr_un addr {};
    socklen_t   addr_len = 0;
    if (!make_sockaddr(path, addr, addr_len)) {
        ::close(fd);
        return {};
    }

    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) != 0) {
        ::close(fd);
        return {};
    }

    return Channel(fd);
}

Channel Channel::listen_seqpacket(const std::string& path, int backlog) {
    auto fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return {};

    sockaddr_un addr {};
    socklen_t   addr_len = 0;
    if (!make_sockaddr(path, addr, addr_len)) {
        ::close(fd);
        return {};
    }

    ::unlink(path.c_str());
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) != 0) {
        ::close(fd);
        return {};
    }

    if (::listen(fd, backlog) != 0) {
        ::close(fd);
        return {};
    }

    return Channel(fd);
}

Channel Channel::accept() const {
    if (fd_ < 0)
        return {};
    auto client_fd = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (client_fd < 0)
        return {};
    return Channel(client_fd);
}

void Channel::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Channel::set_nonblocking(bool enabled) const {
    if (fd_ < 0)
        return false;

    auto flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0)
        return false;

    if (enabled)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    return ::fcntl(fd_, F_SETFL, flags) == 0;
}

ssize_t Channel::send_bytes(const void* data, std::size_t size, int send_fd) const {
    if (fd_ < 0)
        return -1;

    if (send_fd < 0)
        return ::send(fd_, data, size, MSG_NOSIGNAL);

    iovec iov {
        .iov_base = const_cast<void*>(data),
        .iov_len  = size,
    };

    alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(int))> control {};

    msghdr msg {};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = control.data();
    msg.msg_controllen = control.size();

    auto* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_RIGHTS;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &send_fd, sizeof(send_fd));

    return ::sendmsg(fd_, &msg, MSG_NOSIGNAL);
}

ssize_t Channel::receive_bytes(void* data, std::size_t capacity, int* received_fd) const {
    if (received_fd)
        *received_fd = -1;
    if (fd_ < 0)
        return -1;

    iovec iov {
        .iov_base = data,
        .iov_len  = capacity,
    };

    alignas(cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(int))> control {};

    msghdr msg {};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = control.data();
    msg.msg_controllen = control.size();

    auto rc = ::recvmsg(fd_, &msg, 0);
    if (rc <= 0)
        return rc;

    for (auto* cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
            continue;
        if (received_fd && cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
            std::memcpy(received_fd, CMSG_DATA(cmsg), sizeof(int));
        break;
    }

    return rc;
}

} // namespace swm::ipc
