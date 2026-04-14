#include <swm/ipc/shared_buffer.hpp>

#include <cerrno>

#include <sys/mman.h>
#include <unistd.h>

#if __has_include(<sys/memfd.h>)
#include <sys/memfd.h>
#else
#include <sys/syscall.h>
#endif

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

namespace {

int create_memfd(const char* name) {
#if __has_include(<sys/memfd.h>)
    return ::memfd_create(name, MFD_CLOEXEC);
#elif defined(SYS_memfd_create)
    return static_cast<int>(::syscall(SYS_memfd_create, name, MFD_CLOEXEC));
#else
    errno = ENOSYS;
    return -1;
#endif
}

} // namespace

namespace swm::ipc {

SharedBuffer::SharedBuffer(size_t size) : size_(size) {
    fd_ = create_memfd("sirenwm-ipc-buffer");
    if (fd_ < 0)
        return;

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

SharedBuffer::~SharedBuffer() {
    destroy();
}

void SharedBuffer::destroy() noexcept {
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

} // namespace swm::ipc
