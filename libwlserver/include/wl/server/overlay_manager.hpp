#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

struct Overlay {
    uint32_t id      = 0;
    int32_t  x       = 0;
    int32_t  y       = 0;
    int32_t  width   = 0;
    int32_t  height  = 0;
    bool     visible = true;
    std::vector<uint8_t> pixels;
};

class OverlayManager {
public:
    using ChangedCallback = std::function<void()>;

    void set_on_changed(ChangedCallback cb) { on_changed_ = std::move(cb); }

    void create(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h);
    bool update(uint32_t id, int fd, uint32_t size);
    void destroy(uint32_t id);

    const std::unordered_map<uint32_t, Overlay>& overlays() const { return overlays_; }
    const Overlay* overlay_at(int32_t x, int32_t y) const;

private:
    std::unordered_map<uint32_t, Overlay> overlays_;
    ChangedCallback on_changed_;

    void notify();
};
