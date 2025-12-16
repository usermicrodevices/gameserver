#pragma once
#include "logger.hpp"
#include <memory>
#include <map>
#include <set>
#include <functional>
#include <chrono>
#include <atomic>

// Debug categories
enum class DebugCategory {
    NETWORK = 0,
    PROTOCOL,
    PYTHON,
    GAMEPLAY,
    PERFORMANCE,
    MEMORY,
    THREADING,
    SECURITY,
    ALL
};

const char* DebugCategoryToString(DebugCategory category);
DebugCategory DebugCategoryFromString(const std::string& category);

// Debug breakpoints
struct Breakpoint {
    std::string name;
    std::function<bool()> condition;
    std::function<void()> action;
    bool enabled;
    int hit_count;
    int max_hits;

    Breakpoint(const std::string& name,
               std::function<bool()> condition,
               std::function<void()> action = nullptr,
               int max_hits = -1);
};

// Debug metrics
struct DebugMetric {
    std::string name;
    std::atomic<double> value;
    std::atomic<double> min_value;
    std::atomic<double> max_value;
    std::atomic<uint64_t> sample_count;
    std::chrono::steady_clock::time_point start_time;

    DebugMetric(const std::string& name);

    void update(double new_value);
    void increment(double amount = 1.0);
    void reset();

    double get_average() const;
    double get_rate() const; // per second
    std::string to_string() const;
};

// Debug profiler
class DebugProfiler {
private:
    struct ProfileSample {
        std::string name;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::microseconds total_time{0};
        uint64_t call_count{0};
        std::map<std::string, ProfileSample*> children;
        ProfileSample* parent{nullptr};

        double get_average_time() const;
        double get_percentage() const;
        void reset();
    };

    std::mutex mutex_;
    ProfileSample root_;
    ProfileSample* current_sample_{&root_};
    std::map<std::string, ProfileSample*> samples_by_name_;
    bool enabled_{false};

public:
    DebugProfiler();

    void start();
    void stop();
    void reset();

    void begin_sample(const std::string& name);
    void end_sample();

    // RAII scope-based profiling
    class Scope {
    private:
        DebugProfiler& profiler_;
        std::string name_;
        bool active_;

    public:
        Scope(DebugProfiler& profiler, const std::string& name);
        ~Scope();
    };

    std::string generate_report() const;
    void save_report(const std::string& filepath) const;

    bool is_enabled() const { return enabled_; }

private:
    ProfileSample* find_or_create_sample(const std::string& name, ProfileSample* parent);
    void generate_report_recursive(const ProfileSample& sample, int depth,
                                  std::stringstream& ss, double total_time) const;
};

// Debug memory tracker
class DebugMemoryTracker {
private:
    struct Allocation {
        void* ptr;
        size_t size;
        std::string type;
        std::string file;
        int line;
        std::chrono::steady_clock::time_point timestamp;
        std::string stack_trace;
    };

    std::mutex mutex_;
    std::map<void*, Allocation> allocations_;
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> peak_allocated_{0};
    std::atomic<uint64_t> allocation_count_{0};
    std::atomic<uint64_t> deallocation_count_{0};

    bool track_stack_trace_{false};
    std::set<std::string> excluded_types_;

public:
    static DebugMemoryTracker& get_instance();

    void track_allocation(void* ptr, size_t size, const std::string& type = "",
                         const char* file = "", int line = 0);
    void track_deallocation(void* ptr);

    void set_track_stack_trace(bool enabled) { track_stack_trace_ = enabled; }
    void add_excluded_type(const std::string& type) { excluded_types_.insert(type); }

    // Statistics
    struct MemoryStats {
        size_t current_allocated;
        size_t peak_allocated;
        uint64_t total_allocations;
        uint64_t total_deallocations;
        size_t memory_leak_count;
        size_t memory_leak_size;
    };

    MemoryStats get_stats() const;
    std::vector<Allocation> get_leaks() const;
    std::string generate_report() const;
    void save_report(const std::string& filepath) const;

    // Check for specific conditions
    bool check_memory_limit(size_t limit) const;
    bool check_leak_threshold(size_t threshold) const;

private:
    DebugMemoryTracker();
    ~DebugMemoryTracker();

    std::string get_stack_trace(int max_depth = 10) const;
};

// Debug system main class
class DebugSystem {
private:
    static DebugSystem* instance_;

    Logger& logger_;
    DebugProfiler profiler_;
    std::map<std::string, DebugMetric> metrics_;
    std::map<std::string, Breakpoint> breakpoints_;
    std::set<DebugCategory> enabled_categories_;
    std::atomic<bool> enabled_{false};

    // Configuration
    struct Config {
        bool enable_profiling = false;
        bool enable_memory_tracking = false;
        bool enable_breakpoints = true;
        std::set<DebugCategory> default_categories = {
            DebugCategory::NETWORK,
            DebugCategory::ERROR
        };
        std::string log_file = "logs/debug.log";
    } config_;

public:
    static DebugSystem& get_instance();

    void initialize(const Config& config);
    void shutdown();

    // Category management
    void enable_category(DebugCategory category);
    void disable_category(DebugCategory category);
    bool is_category_enabled(DebugCategory category) const;
    void enable_all_categories();
    void disable_all_categories();

    // Logging with categories
    void log(DebugCategory category, LogLevel level, const std::string& message,
             const char* file = "", int line = 0, const char* function = "");

    void logf(DebugCategory category, LogLevel level, const char* format, ...);

    // Convenience methods
    #define DEBUG_LOG(category, level, message) \
        if (DebugSystem::get_instance().is_category_enabled(category)) \
            DebugSystem::get_instance().log(category, level, message, \
                                           __FILE__, __LINE__, __FUNCTION__)

    #define DEBUG_TRACE(category, message) \
        DEBUG_LOG(category, LogLevel::TRACE, message)

    #define DEBUG_INFO(category, message) \
        DEBUG_LOG(category, LogLevel::INFO, message)

    #define DEBUG_WARN(category, message) \
        DEBUG_LOG(category, LogLevel::WARN, message)

    #define DEBUG_ERROR(category, message) \
        DEBUG_LOG(category, LogLevel::ERROR, message)

    // Metrics
    DebugMetric& get_metric(const std::string& name);
    void update_metric(const std::string& name, double value);
    void increment_metric(const std::string& name, double amount = 1.0);
    std::map<std::string, DebugMetric> get_all_metrics() const;
    void reset_all_metrics();

    // Breakpoints
    void add_breakpoint(const std::string& name,
                        std::function<bool()> condition,
                        std::function<void()> action = nullptr,
                        int max_hits = -1);
    void remove_breakpoint(const std::string& name);
    void enable_breakpoint(const std::string& name);
    void disable_breakpoint(const std::string& name);
    void check_breakpoints();

    // Profiling
    DebugProfiler& get_profiler() { return profiler_; }
    void start_profiling() { if (config_.enable_profiling) profiler_.start(); }
    void stop_profiling() { profiler_.stop(); }

    // Memory tracking
    void start_memory_tracking(bool track_stack = false);
    void stop_memory_tracking();
    void report_memory_leaks();

    // State inspection
    std::string generate_debug_report() const;
    void save_debug_report(const std::string& filepath) const;

    // Assertions with custom handling
    static void assert_condition(bool condition, const std::string& message,
                                const char* file, int line, const char* function);

    // Configuration
    const Config& get_config() const { return config_; }
    void update_config(const Config& new_config);

private:
    DebugSystem();
};

// Debug assertion macros
#ifdef NDEBUG
    #define DEBUG_ASSERT(condition, message) do { } while(0)
    #define DEBUG_ASSERT_MSG(condition, message) do { } while(0)
#else
    #define DEBUG_ASSERT(condition, message) \
        do { \
            if (!(condition)) { \
                DebugSystem::assert_condition(false, message, \
                                             __FILE__, __LINE__, __FUNCTION__); \
            } \
        } while(0)

    #define DEBUG_ASSERT_MSG(condition, message) DEBUG_ASSERT(condition, message)
#endif

// Memory tracking macros
#ifdef DEBUG_MEMORY_TRACKING
    #define DEBUG_TRACK_ALLOCATION(ptr, size) \
        DebugMemoryTracker::get_instance().track_allocation(ptr, size, \
                                                           typeid(*ptr).name(), \
                                                           __FILE__, __LINE__)

    #define DEBUG_TRACK_DEALLOCATION(ptr) \
        DebugMemoryTracker::get_instance().track_deallocation(ptr)

    #define DEBUG_NEW new(__FILE__, __LINE__)
    #define DEBUG_DELETE(ptr) \
        do { \
            DebugMemoryTracker::get_instance().track_deallocation(ptr); \
            delete ptr; \
        } while(0)
#else
    #define DEBUG_TRACK_ALLOCATION(ptr, size) do { } while(0)
    #define DEBUG_TRACK_DEALLOCATION(ptr) do { } while(0)
    #define DEBUG_NEW new
    #define DEBUG_DELETE(ptr) delete ptr
#endif

// Performance profiling macros
#ifdef DEBUG_PROFILING
    #define DEBUG_PROFILE_SCOPE(name) \
        DebugProfiler::Scope debug_profile_scope_##__LINE__( \
            DebugSystem::get_instance().get_profiler(), name)

    #define DEBUG_PROFILE_FUNCTION() \
        DEBUG_PROFILE_SCOPE(__FUNCTION__)

    #define DEBUG_PROFILE_BEGIN(name) \
        DebugSystem::get_instance().get_profiler().begin_sample(name)

    #define DEBUG_PROFILE_END() \
        DebugSystem::get_instance().get_profiler().end_sample()
#else
    #define DEBUG_PROFILE_SCOPE(name) do { } while(0)
    #define DEBUG_PROFILE_FUNCTION() do { } while(0)
    #define DEBUG_PROFILE_BEGIN(name) do { } while(0)
    #define DEBUG_PROFILE_END() do { } while(0)
#endif
