// EventDispatcher.hpp
#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

#include "../include/client/InputEvents.hpp"

class EventDispatcher {
public:
    using EventHandler = std::function<void(const Input::InputEvent&)>;
    using EventFilter = std::function<bool(const Input::InputEvent&)>;

    EventDispatcher();
    ~EventDispatcher();

    // Singleton access
    static EventDispatcher& Instance();

    // Event posting (thread-safe)
    void PostEvent(const Input::InputEvent& event);
    void PostEvent(Input::EventType type, const Input::EventData& data);

    // Event subscription
    void Subscribe(Input::EventType type, EventHandler handler,
                   const std::string& subscriber = "");
    void Subscribe(const std::vector<Input::EventType>& types,
                   EventHandler handler, const std::string& subscriber = "");

    void Unsubscribe(Input::EventType type, const std::string& subscriber = "");
    void UnsubscribeAll(const std::string& subscriber);

    // Event filtering
    void AddFilter(Input::EventType type, EventFilter filter);
    void RemoveFilter(Input::EventType type);

    // Processing control
    void StartProcessing();
    void StopProcessing();
    void ProcessEvents();  // For immediate mode processing

    // Statistics
    struct Stats {
        size_t eventsProcessed{0};
        size_t eventsDropped{0};
        size_t activeSubscribers{0};
        size_t queueSize{0};
    };

    Stats GetStats() const;

private:
    void ProcessingThread();
    void DeliverEvent(const Input::InputEvent& event);
    bool ShouldDeliver(const Input::InputEvent& event);

    struct Subscription {
        EventHandler handler;
        std::string subscriber;
    };

    std::unordered_map<Input::EventType, std::vector<Subscription>> subscribers_;
    std::unordered_map<Input::EventType, std::vector<EventFilter>> filters_;
    mutable std::shared_mutex subscribersMutex_;
    mutable std::shared_mutex filtersMutex_;

    std::queue<Input::InputEvent> eventQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;

    std::thread processingThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> immediateMode_{false};

    Stats stats_;
    mutable std::mutex statsMutex_;

    // Event prioritization
    static constexpr size_t MAX_QUEUE_SIZE = 1000;
    std::array<uint8_t, static_cast<size_t>(Input::EventType::WindowClosed) + 1> eventPriorities_;

    void SetupPriorities();
};
