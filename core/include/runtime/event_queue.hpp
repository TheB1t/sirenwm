#pragma once

#include <cassert>
#include <memory>
#include <queue>
#include <utility>

#include <runtime/event_receiver.hpp>

// ---------------------------------------------------------------------------
// Deterministic event pipeline.
//
// The whole WM speaks one event bus: Core, backend, and modules all push
// typed events through IEventSink::post_event. The queue is drained once
// per tick from a single chokepoint (Runtime::drain_events), in strict FIFO,
// with a reentrancy assert that guarantees one delivery pass is active at
// a time. Receivers cannot reorder, skip, or short-circuit the queue.
//
// Dispatch is a composition of:
//   1. Static dispatch — TypedEvent<Ev>::deliver statically resolves
//      receiver.on(ev_) to the correct IEventReceiver overload for Ev
//      at compile time.
//   2. Virtual dispatch — IEventReceiver::on(event::X) is virtual, so the
//      call lands on the concrete subclass's override (BarModule,
//      X11Backend, LuaHost, ...).
//
// No std::variant, no std::visit, no std::function: a fixed list of event
// types is not something we want to maintain at the sink layer, and erasing
// to std::function wastes an allocation per post.
// ---------------------------------------------------------------------------

class QueuedEvent {
    public:
        virtual ~QueuedEvent() = default;
        virtual void deliver(IEventReceiver& r) const = 0;
};

template<typename Ev>
class TypedEvent final : public QueuedEvent {
        Ev ev_;
    public:
        explicit TypedEvent(Ev ev) : ev_(std::move(ev)) {}
        void deliver(IEventReceiver& r) const override { r.on(ev_); }
};

// Injection interface for anyone who needs to post events without owning
// the queue — Core holds a pointer to this, not to Runtime, to keep the
// dependency one-way.
class IEventSink {
    public:
        virtual ~IEventSink() = default;

        template<typename Ev>
        void post_event(Ev ev) {
            post_queued(std::make_unique<TypedEvent<Ev>>(std::move(ev)));
        }

    protected:
        virtual void post_queued(std::unique_ptr<QueuedEvent> ev) = 0;
};

// Null object — drops every posted event on the floor. Used as the default
// sink so call sites never need a null check. The tiny pre-init window
// between Core construction and Runtime wiring the real sink silently
// discards events, which is what we want: nothing is listening yet.
class NullEventSink final : public IEventSink {
    protected:
        void post_queued(std::unique_ptr<QueuedEvent>) override {}
};

inline IEventSink& null_event_sink() {
    static NullEventSink instance;
    return instance;
}

class EventQueue {
        std::queue<std::unique_ptr<QueuedEvent>> q_;
        bool draining_ = false;

    public:
        bool empty()    const { return q_.empty(); }
        bool draining() const { return draining_; }

        void push(std::unique_ptr<QueuedEvent> ev) {
            q_.push(std::move(ev));
        }

        void clear() {
            assert(!draining_ && "EventQueue::clear while draining");
            while (!q_.empty())
                q_.pop();
        }

        // Drain with a receiver callback: caller decides how to broadcast each
        // dequeued event (usually: iterate a list of IEventReceiver pointers).
        template<typename Broadcast>
        void drain(Broadcast&& broadcast) {
            assert(!draining_ && "EventQueue::drain reentered");
            draining_ = true;
            while (!q_.empty()) {
                auto ev = std::move(q_.front());
                q_.pop();
                broadcast(*ev);
            }
            draining_ = false;
        }
};
