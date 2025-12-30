#include <chrono>

#include "../include/client/EventDispatcher.hpp"

EventDispatcher::EventDispatcher() {
    SetupPriorities();
    running_.store(false);
    immediateMode_.store(false);
}

EventDispatcher::~EventDispatcher() {
    StopProcessing();
}

EventDispatcher& EventDispatcher::Instance() {
    static EventDispatcher instance;
    return instance;
}

void EventDispatcher::PostEvent(const Input::InputEvent& event) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (eventQueue_.size() < MAX_QUEUE_SIZE) {
        eventQueue_.push(event);
        queueCondition_.notify_one();
    } else {
        // Drop the event if queue is full
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        stats_.eventsDropped++;
    }
}

void EventDispatcher::PostEvent(Input::EventType type, const Input::EventData& data) {
    Input::InputEvent event;
    event.type = type;
    event.data = data;
    event.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    PostEvent(event);
}

void EventDispatcher::Subscribe(Input::EventType type, EventHandler handler, const std::string& subscriber) {
    std::lock_guard<std::shared_mutex> lock(subscribersMutex_);
    subscribers_[type].push_back({handler, subscriber});
}

void EventDispatcher::Subscribe(const std::vector<Input::EventType>& types, EventHandler handler, const std::string& subscriber) {
    std::lock_guard<std::shared_mutex> lock(subscribersMutex_);
    for (auto type : types) {
        subscribers_[type].push_back({handler, subscriber});
    }
}

void EventDispatcher::Unsubscribe(Input::EventType type, const std::string& subscriber) {
    std::lock_guard<std::shared_mutex> lock(subscribersMutex_);
    auto it = subscribers_.find(type);
    if (it != subscribers_.end()) {
        auto& handlers = it->second;
        handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
            [&subscriber](const Subscription& sub) {
                return sub.subscriber == subscriber;
            }), handlers.end());
    }
}

void EventDispatcher::UnsubscribeAll(const std::string& subscriber) {
    std::lock_guard<std::shared_mutex> lock(subscribersMutex_);
    for (auto& [type, handlers] : subscribers_) {
        handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
            [&subscriber](const Subscription& sub) {
                return sub.subscriber == subscriber;
            }), handlers.end());
    }
}

void EventDispatcher::AddFilter(Input::EventType type, EventFilter filter) {
    std::lock_guard<std::shared_mutex> lock(filtersMutex_);
    filters_[type].push_back(filter);
}

void EventDispatcher::RemoveFilter(Input::EventType type) {
    std::lock_guard<std::shared_mutex> lock(filtersMutex_);
    filters_.erase(type);
}

void EventDispatcher::StartProcessing() {
    if (running_.exchange(true)) return;
    processingThread_ = std::thread(&EventDispatcher::ProcessingThread, this);
}

void EventDispatcher::StopProcessing() {
    running_.store(false);
    queueCondition_.notify_all();
    if (processingThread_.joinable()) {
        processingThread_.join();
    }
}

void EventDispatcher::ProcessEvents() {
    immediateMode_.store(true);
    while (true) {
        Input::InputEvent event;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (eventQueue_.empty()) break;
            event = eventQueue_.front();
            eventQueue_.pop();
        }
        DeliverEvent(event);
    }
    immediateMode_.store(false);
}

void EventDispatcher::ProcessingThread() {
    while (running_.load()) {
        Input::InputEvent event;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this]() {
                return !eventQueue_.empty() || !running_.load();
            });
            if (!running_.load()) break;
            event = eventQueue_.front();
            eventQueue_.pop();
        }
        DeliverEvent(event);
    }
}

void EventDispatcher::DeliverEvent(const Input::InputEvent& event) {
    if (!ShouldDeliver(event)) return;

    std::shared_lock<std::shared_mutex> lock(subscribersMutex_);
    auto it = subscribers_.find(event.type);
    if (it != subscribers_.end()) {
        for (auto& subscription : it->second) {
            subscription.handler(event);
        }
    }

    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        stats_.eventsProcessed++;
    }
}

bool EventDispatcher::ShouldDeliver(const Input::InputEvent& event) {
    std::shared_lock<std::shared_mutex> lock(filtersMutex_);
    auto it = filters_.find(event.type);
    if (it != filters_.end()) {
        for (auto& filter : it->second) {
            if (!filter(event)) return false;
        }
    }
    return true;
}

EventDispatcher::Stats EventDispatcher::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.queueSize = eventQueue_.size();
    return stats_;
}

void EventDispatcher::SetupPriorities() {
    // Set priorities for event types (lower number = higher priority)
    eventPriorities_[static_cast<size_t>(Input::EventType::WindowClosed)] = 0;
    eventPriorities_[static_cast<size_t>(Input::EventType::KeyPressed)] = 1;
    eventPriorities_[static_cast<size_t>(Input::EventType::KeyReleased)] = 1;
    eventPriorities_[static_cast<size_t>(Input::EventType::MouseButtonPressed)] = 1;
    eventPriorities_[static_cast<size_t>(Input::EventType::MouseButtonReleased)] = 1;
    eventPriorities_[static_cast<size_t>(Input::EventType::MouseMoved)] = 2;
    eventPriorities_[static_cast<size_t>(Input::EventType::MouseWheel)] = 2;
    eventPriorities_[static_cast<size_t>(Input::EventType::WindowResized)] = 3;
    eventPriorities_[static_cast<size_t>(Input::EventType::TextInput)] = 4;
}
