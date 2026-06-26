// broadcast.hpp — channel broadcasting (Laravel: Illuminate\Contracts\Broadcasting).
//
// Publish events to named channels; subscribers on that channel receive them. This is
// the in-process bus — a production driver would push to WebSocket/Pusher/Reverb
// clients, but the channel/event/payload shape is the same. Header-only.
#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Broadcaster {
public:
    using Subscriber = std::function<void(const std::string& event, const std::string& payload)>;

    void subscribe(const std::string& channel, Subscriber s) {
        channels_[channel].push_back(std::move(s));
    }

    void broadcast(const std::string& channel, const std::string& event,
                   const std::string& payload = "") {
        auto it = channels_.find(channel);
        if (it == channels_.end()) return;
        for (auto& s : it->second) s(event, payload);
    }

    std::size_t subscribers(const std::string& channel) const {
        auto it = channels_.find(channel);
        return it == channels_.end() ? 0 : it->second.size();
    }

private:
    std::unordered_map<std::string, std::vector<Subscriber>> channels_;
};
