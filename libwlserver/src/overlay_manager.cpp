#include <wl/server/overlay_manager.hpp>

#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

void OverlayManager::create(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h) {
    overlays_[id] = Overlay{.id = id, .x = x, .y = y, .width = w, .height = h, .visible = true, .pixels = {}};
    notify();
}

bool OverlayManager::update(uint32_t id, int fd, uint32_t size) {
    auto it = overlays_.find(id);
    if (it == overlays_.end()) {
        close(fd); return false;
    }
    auto& ov = it->second;
    if (ov.width <= 0 || ov.height <= 0) {
        close(fd); return false;
    }
    size_t expected = static_cast<size_t>(ov.width) * ov.height * 4;
    if (static_cast<size_t>(size) != expected) {
        close(fd); return false;
    }

    void* data = mmap(nullptr, expected, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (data == MAP_FAILED) return false;
    ov.pixels.resize(expected);
    memcpy(ov.pixels.data(), data, expected);
    munmap(data, expected);
    notify();
    return true;
}

void OverlayManager::destroy(uint32_t id) {
    overlays_.erase(id);
    notify();
}

const Overlay* OverlayManager::overlay_at(int32_t x, int32_t y) const {
    for (auto& [id, ov] : overlays_) {
        if (!ov.visible) continue;
        if (x >= ov.x && x < ov.x + ov.width && y >= ov.y && y < ov.y + ov.height)
            return &ov;
    }
    return nullptr;
}

void OverlayManager::notify() {
    if (on_changed_) on_changed_();
}
