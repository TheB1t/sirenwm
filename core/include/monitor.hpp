#pragma once

#include <algorithm>
#include <string>
#include <memory>
#include <utility>

#include <vec.hpp>

struct Workspace;

enum class MonitorEdge : uint8_t { Top, Bottom, Left, Right };

struct Monitor {
    int         id;
    std::string name;

    Vec2i       pos_;
    Vec2i       size_;

    int         active_ws = -1;

    // Reserved areas per edge — how much space is currently claimed at each
    // side of this monitor (e.g. by bars or panels). pos_/size_ already have
    // these subtracted (workspace area); `physical()` undoes the reservations
    // to recover the original monitor rect.
    int top_inset_    = 0;
    int bottom_inset_ = 0;
    int left_inset_   = 0;
    int right_inset_  = 0;

    Monitor(int id, std::string name, int x, int y, int w, int h)
        : id(id), name(std::move(name)), pos_(x, y), size_(w, h) {}

    // Vec2 accessors
    Vec2i&       pos()        { return pos_; }
    const Vec2i& pos()  const { return pos_; }
    Vec2i&       size()       { return size_; }
    const Vec2i& size() const { return size_; }

    int top_inset()    const { return top_inset_; }
    int bottom_inset() const { return bottom_inset_; }
    int left_inset()   const { return left_inset_; }
    int right_inset()  const { return right_inset_; }
    int inset(MonitorEdge e) const {
        switch (e) {
            case MonitorEdge::Top:    return top_inset_;
            case MonitorEdge::Bottom: return bottom_inset_;
            case MonitorEdge::Left:   return left_inset_;
            case MonitorEdge::Right:  return right_inset_;
        }
        return 0;
    }

    Vec2i center() const { return pos_ + size_ / 2; }

    bool contains(Vec2i p) const {
        return p.x() >= pos_.x() && p.x() < pos_.x() + size_.x() &&
               p.y() >= pos_.y() && p.y() < pos_.y() + size_.y();
    }

    // Physical area: the full monitor surface before edge reservations.
    std::pair<Vec2i, Vec2i> physical() const {
        int top   = std::max(0, top_inset_);
        int bot   = std::max(0, bottom_inset_);
        int left  = std::max(0, left_inset_);
        int right = std::max(0, right_inset_);
        return { { pos_.x() - left, pos_.y() - top },
            { std::max(1, size_.x() + left + right),
              std::max(1, size_.y() + top + bot) } };
    }

    bool physical_contains(Vec2i p) const {
        auto [pp, ps] = physical();
        return p.x() >= pp.x() && p.x() < pp.x() + ps.x() &&
               p.y() >= pp.y() && p.y() < pp.y() + ps.y();
    }
};

using MonitorState = Monitor;
