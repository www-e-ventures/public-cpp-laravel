// events.hpp — event dispatcher (Laravel: Illuminate\Events\Dispatcher).
//
// Type-erased by event type (same trick as the container): listen<E>() registers a
// handler keyed by typeid(E); dispatch(e) invokes every handler for that type. Plain
// value events, no string names. Header-only.
#pragma once
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "queue.hpp"

// Class-based listener (Laravel: a listener class with handle()).
template <typename E>
class Listener {
public:
    virtual ~Listener() = default;
    virtual void handle(const E& event) = 0;
};

class EventDispatcher {
public:
    template <typename E>
    void listen(std::function<void(const E&)> handler) {
        listeners_[std::type_index(typeid(E))].push_back(
            [handler](const std::any& e) { handler(std::any_cast<const E&>(e)); });
    }

    // Register a listener object; kept alive by the captured shared_ptr.
    template <typename E>
    void subscribe(std::shared_ptr<Listener<E>> listener) {
        listen<E>([listener](const E& e) { listener->handle(e); });
    }

    // Queued listener: on dispatch, enqueue the handler (with a copy of the event)
    // onto `queue` instead of running it inline. Process later via the queue's worker.
    template <typename E>
    void listen_queued(QueueContract& queue, std::function<void(const E&)> handler) {
        listen<E>([&queue, handler](const E& e) {
            E copy = e;
            queue.push([handler, copy] { handler(copy); });
        });
    }

    template <typename E>
    void dispatch(const E& event) {
        auto it = listeners_.find(std::type_index(typeid(E)));
        if (it == listeners_.end()) return;
        std::any boxed = event;
        for (auto& l : it->second) l(boxed);
    }

    template <typename E>
    std::size_t listener_count() const {
        auto it = listeners_.find(std::type_index(typeid(E)));
        return it == listeners_.end() ? 0 : it->second.size();
    }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const std::any&)>>> listeners_;
};
