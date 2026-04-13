#include <wl/shm_buffer.hpp>

#include <sys/mman.h>
#include <unistd.h>

#if __has_include(<sys/memfd.h>)
#include <sys/memfd.h>
#endif

namespace wl {

ShmBuffer::ShmBuffer(size_t size) : size_(size) {
    fd_ = memfd_create("wl-shm-buffer", MFD_CLOEXEC);
    if (fd_ < 0) return;

    if (ftruncate(fd_, static_cast<off_t>(size)) != 0) {
        close(fd_);
        fd_ = -1;
        return;
    }

    data_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (data_ == MAP_FAILED) {
        data_ = nullptr;
        close(fd_);
        fd_ = -1;
    }
}

ShmBuffer::~ShmBuffer() {
    destroy();
}

void ShmBuffer::destroy() noexcept {
    if (data_) {
        munmap(data_, size_);
        data_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    size_ = 0;
}

} // namespace wl
