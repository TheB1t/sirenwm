#pragma once

#include <functional>
#include <vector>
#include <algorithm>
#include <sys/epoll.h>
#include <unistd.h>

#include <support/log.hpp>

class EventLoop {
public:
    EventLoop() = default;
    ~EventLoop() { stop(); }

    EventLoop(const EventLoop&)            = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&)                 = delete;
    EventLoop& operator=(EventLoop&&)      = delete;

    void start() {
        if (epoll_fd_ >= 0)
            return;
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) {
            LOG_ERR("EventLoop: epoll_create1 failed");
            std::abort();
        }
        for (auto& w : entries_)
            epoll_add(w.fd);
    }

    void stop() {
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
            epoll_fd_ = -1;
        }
    }

    bool running() const { return epoll_fd_ >= 0; }

    void watch(int fd, std::function<void()> cb) {
        entries_.push_back({ fd, std::move(cb) });
        if (epoll_fd_ >= 0)
            epoll_add(fd);
    }

    void unwatch(int fd) {
        if (epoll_fd_ >= 0)
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        std::erase_if(entries_, [fd](const Entry& e) { return e.fd == fd; });
    }

    int poll(int timeout_ms = 100) {
        if (epoll_fd_ < 0)
            return 0;

        struct epoll_event ready[kMaxEvents];
        int n = epoll_wait(epoll_fd_, ready, kMaxEvents, timeout_ms);

        for (int i = 0; i < n; ++i) {
            int fd = ready[i].data.fd;
            for (auto& e : entries_) {
                if (e.fd == fd) {
                    e.cb();
                    break;
                }
            }
        }
        return n;
    }

    class FdHandle {
        EventLoop* loop_ = nullptr;
        int        fd_   = -1;
    public:
        FdHandle() = default;
        FdHandle(EventLoop& loop, int fd, std::function<void()> cb)
            : loop_(&loop), fd_(fd) {
            loop.watch(fd, std::move(cb));
        }

        FdHandle(const FdHandle&)            = delete;
        FdHandle& operator=(const FdHandle&) = delete;

        FdHandle(FdHandle&& o) noexcept
            : loop_(o.loop_), fd_(o.fd_) {
            o.loop_ = nullptr;
            o.fd_   = -1;
        }

        FdHandle& operator=(FdHandle&& o) noexcept {
            if (this != &o) {
                reset();
                loop_ = o.loop_;
                fd_   = o.fd_;
                o.loop_ = nullptr;
                o.fd_   = -1;
            }
            return *this;
        }

        ~FdHandle() { reset(); }

        int  fd() const { return fd_; }
        explicit operator bool() const { return fd_ >= 0; }

        void reset() {
            if (fd_ >= 0 && loop_)
                loop_->unwatch(fd_);
            if (fd_ >= 0)
                ::close(fd_);
            loop_ = nullptr;
            fd_   = -1;
        }
    };

private:
    static constexpr int kMaxEvents = 16;

    struct Entry {
        int                   fd;
        std::function<void()> cb;
    };

    std::vector<Entry> entries_;
    int                epoll_fd_ = -1;

    void epoll_add(int fd) {
        struct epoll_event ev = {};
        ev.events  = EPOLLIN;
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }
};
