#pragma once

#include <cstddef>
#include <cstring>
#include <utility>

namespace swm::ipc {

class SharedBuffer {
public:
    SharedBuffer() noexcept = default;

    explicit SharedBuffer(size_t size);

    ~SharedBuffer();

    SharedBuffer(const SharedBuffer&)            = delete;
    SharedBuffer& operator=(const SharedBuffer&) = delete;

    SharedBuffer(SharedBuffer&& other) noexcept
        : fd_(std::exchange(other.fd_, -1))
        , data_(std::exchange(other.data_, nullptr))
        , size_(std::exchange(other.size_, 0)) {}

    SharedBuffer& operator=(SharedBuffer&& other) noexcept {
        if (this != &other) {
            destroy();
            fd_   = std::exchange(other.fd_, -1);
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    explicit operator bool() const noexcept { return data_ != nullptr; }

    void*  data() const noexcept { return data_; }
    size_t size() const noexcept { return size_; }

    void write(const void* src, size_t len) {
        if (data_ && len <= size_)
            std::memcpy(data_, src, len);
    }

    int release_fd() noexcept {
        int released = fd_;
        fd_ = -1;
        return released;
    }

private:
    int    fd_   = -1;
    void*  data_ = nullptr;
    size_t size_ = 0;

    void destroy() noexcept;
};

} // namespace swm::ipc
