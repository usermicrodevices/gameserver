#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>

namespace PythonScripting {

// Event handler interface
class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    virtual bool HandleEvent(const nlohmann::json& eventData) = 0;
    virtual std::string GetName() const = 0;
    virtual int GetPriority() const = 0;
};

// Python-based event handler
class PythonEventHandler : public IEventHandler {
public:
    PythonEventHandler(const std::string& name,
                      const std::string& moduleName,
                      const std::string& functionName,
                      int priority = 0);

    bool HandleEvent(const nlohmann::json& eventData) override;
    std::string GetName() const override { return name_; }
    int GetPriority() const override { return priority_; }

    bool IsValid() const;

private:
    std::string name_;
    std::string moduleName_;
    std::string functionName_;
    int priority_;
};

// Event dispatcher
class EventDispatcher {
public:
    static EventDispatcher& GetInstance();

    void RegisterHandler(const std::string& eventName,
                        std::shared_ptr<IEventHandler> handler);
    void UnregisterHandler(const std::string& eventName,
                          const std::string& handlerName);

    bool DispatchEvent(const std::string& eventName,
                      const nlohmann::json& eventData);

    std::vector<std::string> GetRegisteredEvents() const;
    std::vector<std::string> GetHandlersForEvent(const std::string& eventName) const;

private:
    EventDispatcher() = default;

    mutable std::shared_mutex handlersMutex_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<IEventHandler>>> handlers_;
};

// Event queue for async processing
class EventQueue {
public:
    struct QueuedEvent {
        std::string name;
        nlohmann::json data;
        int64_t timestamp;
        int priority;

        bool operator<(const QueuedEvent& other) const {
            // Higher priority events come first
            if (priority != other.priority) return priority < other.priority;
            // Older events come first
            return timestamp > other.timestamp;
        }
    };

    EventQueue(size_t maxSize = 10000);
    ~EventQueue();

    bool PushEvent(const std::string& eventName,
                  const nlohmann::json& eventData,
                  int priority = 0);

    bool PopEvent(QueuedEvent& event);

    size_t Size() const;
    size_t Capacity() const;
    void Clear();

    void StartProcessing();
    void StopProcessing();

private:
    void ProcessLoop();

    size_t maxSize_;
    std::priority_queue<QueuedEvent> queue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCV_;

    std::atomic<bool> running_;
    std::thread processThread_;
};

// Scheduled event
class ScheduledEvent {
public:
    ScheduledEvent(const std::string& eventName,
                  const nlohmann::json& eventData,
                  int64_t executeAt,
                  bool repeat = false,
                  int64_t interval = 0);

    bool ShouldExecute() const;
    bool Execute();
    void Reschedule();

    std::string GetName() const { return eventName_; }
    int64_t GetExecuteAt() const { return executeAt_; }

private:
    std::string eventName_;
    nlohmann::json eventData_;
    int64_t executeAt_;
    bool repeat_;
    int64_t interval_;
};

// Event scheduler
class EventScheduler {
public:
    static EventScheduler& GetInstance();

    void ScheduleEvent(const std::string& eventName,
                      const nlohmann::json& eventData,
                      int64_t delayMs,
                      bool repeat = false,
                      int64_t intervalMs = 0);

    void CancelEvent(const std::string& eventName);
    void Update();

private:
    EventScheduler();
    ~EventScheduler();

    void ProcessScheduledEvents();

    mutable std::mutex scheduledEventsMutex_;
    std::vector<std::unique_ptr<ScheduledEvent>> scheduledEvents_;

    std::atomic<bool> running_;
    std::thread schedulerThread_;
};

} // namespace PythonScripting
