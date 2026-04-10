#pragma once

#include <algorithm>
#include <string>
#include <memory>
#include <utility>

#include <vec.hpp>

struct Workspace;

struct Monitor {
    int         id;
    std::string name;

    Vec2i       pos_;
    Vec2i       size_;

    int         active_ws = -1;

    // Bar insets — how much space is currently reserved at top/bottom of this
    // monitor by bars. pos_/size_ already have these subtracted (workspace area),
    // so `physical()` undoes them to recover the original monitor rect.
    int         top_inset_    = 0;
    int         bottom_inset_ = 0;

    Monitor(int id, std::string name, int x, int y, int w, int h)
        : id(id), name(std::move(name)), pos_(x, y), size_(w, h) {}

    // Scalar accessors (backward compat)
    int&       x()             { return pos_.x(); }
    int        x()       const { return pos_.x(); }
    int&       y()             { return pos_.y(); }
    int        y()       const { return pos_.y(); }
    int&       width()         { return size_.x(); }
    int        width()   const { return size_.x(); }
    int&       height()        { return size_.y(); }
    int        height()  const { return size_.y(); }

    // Vec2 accessors
    Vec2i&       pos()        { return pos_; }
    const Vec2i& pos()  const { return pos_; }
    Vec2i&       size()       { return size_; }
    const Vec2i& size() const { return size_; }

    int top_inset()    const { return top_inset_; }
    int bottom_inset() const { return bottom_inset_; }

    Vec2i center() const { return pos_ + size_ / 2; }

    bool contains(Vec2i p) const {
        return p.x() >= pos_.x() && p.x() < pos_.x() + size_.x() &&
               p.y() >= pos_.y() && p.y() < pos_.y() + size_.y();
    }

    // Physical area: the full monitor surface before bar insets were applied.
    std::pair<Vec2i, Vec2i> physical() const {
        int top = std::max(0, top_inset_);
        int bot = std::max(0, bottom_inset_);
        return { { pos_.x(), pos_.y() - top }, { size_.x(), std::max(1, size_.y() + top + bot) } };
    }

    bool physical_contains(Vec2i p) const {
        auto [pp, ps] = physical();
        return p.x() >= pp.x() && p.x() < pp.x() + ps.x() &&
               p.y() >= pp.y() && p.y() < pp.y() + ps.y();
    }
};

using MonitorState = Monitor;
