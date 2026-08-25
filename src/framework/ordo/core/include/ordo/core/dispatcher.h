#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string_view>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace ordo::core {

// Opaque per-subscription identifier, unique within one Dispatcher instance.
// Currently informational only -- removal is by owner (see unsubscribe).
using SubscriptionId = std::size_t;

// Trace record for one dispatch, delivered to the observer (if any) before
// the handlers run -- including zero-subscriber dispatches.
struct DispatchRecord {
    std::size_t typeHash{};
    std::string_view eventName;    // empty when EventT has no eventName member
    std::size_t subscriberCount{};
};

// Typed-struct synchronous event bus. The struct type passed to
// subscribe<EventT>/dispatch<EventT> IS the event identity and payload.
class Dispatcher {
public:
    Dispatcher() = default;

    // owner is an opaque cookie (never dereferenced) for bulk removal via
    // unsubscribe(owner); handlers for the same EventT fire in subscribe
    // order. Pass the subscribing component's OWN address (policy-ordo-
    // roles.md) -- a foreign pointer silently detaches someone else's handlers.
    template <typename EventT>
    SubscriptionId subscribe(const void* owner, std::function<void(const EventT&)> handler) {
        const SubscriptionId id = nextId_++;
        auto trampoline = [handler = std::move(handler)](const void* event) {
            handler(*static_cast<const EventT*>(event));
        };
        handlers_[typeid(EventT).hash_code()].push_back(Entry{owner, id, std::move(trampoline)});
        return id;
    }

    // Removes every subscription (any event type) registered with this owner.
    void unsubscribe(const void* owner);

    // Installs (or, with an empty function, clears) the single trace observer
    // slot. The intended client is the debug bridge.
    // Contract (honor-system): the observer must not subscribe, unsubscribe,
    // or dispatch -- it is trace-only and unguarded against reentrancy.
    void setObserver(std::function<void(const DispatchRecord&)> observer);

    // Synchronous 1:N broadcast to EventT subscribers, in subscribe order.
    // Reentrancy: snapshots the subscriber list up front -- mid-dispatch
    // subscribes join the live vector (fire next dispatch); mid-dispatch
    // unsubscribes are skipped via a live re-check by id. An observer (if
    // set) sees every dispatch, even zero-subscriber ones, before the loop runs.
    template <typename EventT>
    void dispatch(const EventT& event) {
        const std::size_t hash = typeid(EventT).hash_code();
        auto it = handlers_.find(hash);

        if (observer_) {
            DispatchRecord record;
            record.typeHash = hash;
            record.subscriberCount = (it != handlers_.end()) ? it->second.size() : 0;
            if constexpr (requires { EventT::eventName; }) {
                record.eventName = EventT::eventName;
            }
            observer_(record);
        }

        if (it == handlers_.end()) {
            return;
        }
        const std::vector<Entry> snapshot = it->second;

        for (const Entry& entry : snapshot) {
            const std::vector<Entry>& live = handlers_[hash];
            const bool stillSubscribed =
                std::any_of(live.begin(), live.end(), [&entry](const Entry& e) { return e.id == entry.id; });
            if (stillSubscribed) {
                entry.trampoline(&event);
            }
        }
    }

private:
    struct Entry {
        const void* owner;
        SubscriptionId id;
        std::function<void(const void*)> trampoline;
    };

    std::unordered_map<std::size_t, std::vector<Entry>> handlers_;
    SubscriptionId nextId_ = 1;
    std::function<void(const DispatchRecord&)> observer_;
};

}  // namespace ordo::core
