#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace wl {

// RAII wrapper for a memfd-backed shared memory buffer.
// Used by overlay rendering to pass pixel data to the display server.
class ShmBuffer {
    public:
        ShmBuffer() noexcept = default;

        explicit ShmBuffer(size_t size);

        ~ShmBuffer();

        ShmBuffer(const ShmBuffer&)            = delete;
        ShmBuffer& operator=(const ShmBuffer&) = delete;

        ShmBuffer(ShmBuffer&& other) noexcept
            : fd_(std::exchange(other.fd_, -1))
              , data_(std::exchange(other.data_, nullptr))
              , size_(std::exchange(other.size_, 0)) {}

        ShmBuffer& operator=(ShmBuffer&& other) noexcept {
            if (this != &other) {
                destroy();
                fd_   = std::exchange(other.fd_, -1);
                data_ = std::exchange(other.data_, nullptr);
                size_ = std::exchange(other.size_, 0);
            }
            return *this;
        }

        explicit operator bool() const noexcept { return data_ != nullptr; }

        void*  data()  const noexcept { return data_; }
        size_t size()  const noexcept { return size_; }

        void write(const void* src, size_t len) {
            if (data_ && len <= size_)
                std::memcpy(data_, src, len);
        }

        // Release the fd for transfer (caller takes ownership).
        // The mapping remains valid until this object is destroyed.
        int release_fd() noexcept {
            int r = fd_;
            fd_ = -1;
            return r;
        }

    private:
        int    fd_   = -1;
        void*  data_ = nullptr;
        size_t size_ = 0;

        void destroy() noexcept;
};

} // namespace wl
