#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <backend/events.hpp>
#include <backend/commands.hpp>
#include <config/config_types.hpp>
#include <domain/core.hpp>
#include <domain/monitor.hpp>
#include <runtime/event_queue.hpp>
#include <runtime/event_receiver.hpp>

using CoreDomainEvent = std::variant<
    event::FocusChanged,
    event::WorkspaceSwitched,
    event::RaiseDocks,
    event::DisplayTopologyChanged,
    event::WindowAssignedToWorkspace
>;

class CoreDomainEventRecorder : public IEventReceiver {
    public:
        std::vector<CoreDomainEvent> events;

        void on(event::FocusChanged ev)              override { events.emplace_back(ev); }
        void on(event::WorkspaceSwitched ev)         override { events.emplace_back(ev); }
        void on(event::RaiseDocks ev)                override { events.emplace_back(ev); }
        void on(event::DisplayTopologyChanged ev)    override { events.emplace_back(ev); }
        void on(event::WindowAssignedToWorkspace ev) override { events.emplace_back(ev); }
};

class DirectEventSink final : public IEventSink {
        EventQueue                   queue_;
        std::vector<IEventReceiver*> receivers_;

    public:
        void add_receiver(IEventReceiver* receiver) {
            receivers_.push_back(receiver);
        }

        void remove_receiver(IEventReceiver* receiver) {
            receivers_.erase(
                std::remove(receivers_.begin(), receivers_.end(), receiver),
                receivers_.end());
        }

        void drain() {
            queue_.drain([this](const QueuedEvent& ev) {
                for (auto* receiver : receivers_) {
                    if (receiver)
                        ev.deliver(*receiver);
                }
            });
        }

    protected:
        void post_queued(std::unique_ptr<QueuedEvent> ev) override {
            queue_.push(std::move(ev));
        }
};

inline Monitor make_monitor(int id, int x, int y, int w, int h,
    const std::string& name = "") {
    return Monitor(id, name.empty() ? ("mon" + std::to_string(id)) : name, x, y, w, h);
}

struct CoreHarness {
        DirectEventSink          event_sink;
        CoreDomainEventRecorder  recorder;
        Core                     core;
        std::vector<Monitor>     monitors;

        explicit CoreHarness(std::vector<Monitor> mons = {})
            : monitors(mons.empty()
                    ? std::vector<Monitor>{ make_monitor(0, 0, 0, 1920, 1080, "primary") }
                    : std::move(mons)) {
            event_sink.add_receiver(&recorder);
            core.set_event_sink(&event_sink);
        }

        void start(std::optional<CoreSettings> settings = std::nullopt) {
            CoreSettings next = settings.has_value()
                ? std::move(*settings)
                : core.current_settings();
            if (next.workspace_defs.empty())
                next.workspace_defs = {{ "[1]", "" }, { "[2]", "" }, { "[3]", "" }};

            core.apply_settings(std::move(next));
            core.init(monitors);
            core.mark_runtime_started(true);
            event_sink.drain();
        }

        WindowId map_window(WindowId id, int ws = 0) {
            core.dispatch(command::atom::EnsureWindow{ id, ws });
            core.dispatch(command::atom::SetWindowMapped{ id, true });
            return id;
        }

        std::vector<CoreDomainEvent> take_core_events() {
            event_sink.drain();
            auto out = std::move(recorder.events);
            recorder.events.clear();
            return out;
        }

        ~CoreHarness() {
            event_sink.drain();
            core.mark_runtime_started(false);
            core.set_event_sink(nullptr);
            event_sink.remove_receiver(&recorder);
        }
};
