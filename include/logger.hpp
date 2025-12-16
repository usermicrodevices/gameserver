#pragma once
#include <string>
#include <memory>
#include <fstream>
#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <vector>
#include <map>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <iomanip>
#include <ctime>
#include <cstdarg>
#include <system_error>
#include <filesystem>

namespace fs = std::filesystem;

// Log levels
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5,
    OFF = 6
};

// Convert log level to string
const char* LogLevelToString(LogLevel level);
LogLevel LogLevelFromString(const std::string& level);

// Log entry structure
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string logger_name;
    std::string message;
    std::string file;
    int line;
    std::string function;
    std::thread::id thread_id;
    std::string process_id;

    // Serialize to string
    std::string to_string() const;
    std::string to_json() const;
    std::string to_csv() const;
};

// Log sink interface
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void write(const LogEntry& entry) = 0;
    virtual void flush() = 0;
    virtual std::string get_name() const = 0;
};

// Console sink with colors
class ConsoleSink : public LogSink {
private:
    std::mutex mutex_;
    bool use_colors_;
    bool use_timestamps_;

public:
    ConsoleSink(bool use_colors = true, bool use_timestamps = true);
    void write(const LogEntry& entry) override;
    void flush() override;
    std::string get_name() const override { return "ConsoleSink"; }

private:
    std::string get_color_code(LogLevel level) const;
    void reset_color() const;
};

// File sink with rotation
class FileSink : public LogSink {
private:
    std::mutex mutex_;
    std::ofstream file_stream_;
    std::string file_path_;
    std::string base_name_;
    std::string extension_;
    size_t max_size_;
    size_t max_files_;
    size_t current_size_;
    bool compress_old_;

public:
    FileSink(const std::string& file_path,
             size_t max_size = 10 * 1024 * 1024, // 10MB
             size_t max_files = 10,
             bool compress_old = false);
    ~FileSink();

    void write(const LogEntry& entry) override;
    void flush() override;
    std::string get_name() const override { return "FileSink"; }

    // File rotation
    void rotate_file();
    void compress_file(const std::string& path);

    // Statistics
    size_t get_current_size() const { return current_size_; }
    size_t get_written_count() const;

private:
    void open_file();
    void close_file();
    bool should_rotate() const;
    std::string get_rotated_path(int index) const;
    void cleanup_old_files();
};

// Network sink (send logs over network)
class NetworkSink : public LogSink {
private:
    std::mutex mutex_;
    std::string host_;
    uint16_t port_;
    int socket_fd_;
    bool connected_;
    std::atomic<bool> reconnect_{false};
    std::thread reconnect_thread_;

public:
    NetworkSink(const std::string& host, uint16_t port);
    ~NetworkSink();

    void write(const LogEntry& entry) override;
    void flush() override;
    std::string get_name() const override { return "NetworkSink"; }

    bool is_connected() const { return connected_; }
    void reconnect();

private:
    bool connect();
    void disconnect();
    void reconnect_loop();
};

// Async sink wrapper (for performance)
class AsyncSink : public LogSink {
private:
    std::unique_ptr<LogSink> inner_sink_;
    std::queue<LogEntry> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> dropped_{0};
    size_t max_queue_size_;

public:
    AsyncSink(std::unique_ptr<LogSink> sink, size_t max_queue_size = 10000);
    ~AsyncSink();

    void write(const LogEntry& entry) override;
    void flush() override;
    std::string get_name() const override;

    size_t get_queue_size() const;
    size_t get_dropped_count() const { return dropped_; }

private:
    void worker_loop();
};

// Filter sink (filter logs by level, pattern, etc.)
class FilterSink : public LogSink {
private:
    std::unique_ptr<LogSink> inner_sink_;
    LogLevel min_level_;
    LogLevel max_level_;
    std::string pattern_;
    std::vector<std::string> excluded_files_;
    std::vector<std::string> excluded_functions_;

public:
    FilterSink(std::unique_ptr<LogSink> sink,
               LogLevel min_level = LogLevel::TRACE,
               LogLevel max_level = LogLevel::FATAL,
               const std::string& pattern = "");

    void write(const LogEntry& entry) override;
    void flush() override;
    std::string get_name() const override;

    void set_min_level(LogLevel level) { min_level_ = level; }
    void set_max_level(LogLevel level) { max_level_ = level; }
    void set_pattern(const std::string& pattern) { pattern_ = pattern; }
    void add_excluded_file(const std::string& file) { excluded_files_.push_back(file); }
    void add_excluded_function(const std::string& func) { excluded_functions_.push_back(func); }

private:
    bool should_log(const LogEntry& entry) const;
};

// Logger configuration
struct LoggerConfig {
    std::string name;
    LogLevel level = LogLevel::INFO;
    std::vector<std::unique_ptr<LogSink>> sinks;
    bool async = false;
    size_t async_queue_size = 10000;
    bool propagate = true; // Propagate to parent loggers

    // Pattern for formatting
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%t] %m";

    // File sink options
    struct FileOptions {
        std::string path = "logs/app.log";
        size_t max_size = 10 * 1024 * 1024; // 10MB
        size_t max_files = 10;
        bool compress = false;
        bool daily_rotation = false;
    } file_options;

    // Network sink options
    struct NetworkOptions {
        std::string host = "localhost";
        uint16_t port = 514; // Syslog port
        bool enabled = false;
    } network_options;

    // Console sink options
    struct ConsoleOptions {
        bool enabled = true;
        bool colors = true;
        bool timestamps = true;
    } console_options;
};

// Main logger class
class Logger {
private:
    std::string name_;
    LogLevel level_;
    std::vector<std::unique_ptr<LogSink>> sinks_;
    Logger* parent_;
    static std::mutex registry_mutex_;
    static std::map<std::string, Logger*> registry_;

    // Performance counters
    std::atomic<uint64_t> log_count_{0};
    std::atomic<uint64_t> error_count_{0};
    std::chrono::steady_clock::time_point creation_time_;

    Logger(const std::string& name, LogLevel level = LogLevel::INFO);

public:
    ~Logger();

    // Factory methods
    static Logger& get_logger(const std::string& name = "root");
    static Logger& create_logger(const LoggerConfig& config);
    static void destroy_logger(const std::string& name);
    static std::vector<std::string> get_logger_names();

    // Logging methods
    void log(LogLevel level, const std::string& message,
             const char* file = __builtin_FILE(),
             int line = __builtin_LINE(),
             const char* function = __builtin_FUNCTION());

    void logf(LogLevel level, const char* format, ...)
        __attribute__((format(printf, 3, 4)));

    void logv(LogLevel level, const char* format, va_list args);

    // Convenience methods
    void trace(const std::string& message,
               const char* file = __builtin_FILE(),
               int line = __builtin_LINE(),
               const char* function = __builtin_FUNCTION());

    void debug(const std::string& message,
               const char* file = __builtin_FILE(),
               int line = __builtin_LINE(),
               const char* function = __builtin_FUNCTION());

    void info(const std::string& message,
              const char* file = __builtin_FILE(),
              int line = __builtin_LINE(),
              const char* function = __builtin_FUNCTION());

    void warn(const std::string& message,
              const char* file = __builtin_FILE(),
              int line = __builtin_LINE(),
              const char* function = __builtin_FUNCTION());

    void error(const std::string& message,
               const char* file = __builtin_FILE(),
               int line = __builtin_LINE(),
               const char* function = __builtin_FUNCTION());

    void fatal(const std::string& message,
               const char* file = __builtin_FILE(),
               int line = __builtin_LINE(),
               const char* function = __builtin_FUNCTION());

    // Sink management
    void add_sink(std::unique_ptr<LogSink> sink);
    void remove_sink(const std::string& sink_name);
    void clear_sinks();

    // Configuration
    void set_level(LogLevel level) { level_ = level; }
    LogLevel get_level() const { return level_; }
    const std::string& get_name() const { return name_; }

    void set_parent(Logger* parent) { parent_ = parent; }
    Logger* get_parent() const { return parent_; }

    // Performance statistics
    struct Statistics {
        uint64_t total_logs;
        uint64_t error_logs;
        double logs_per_second;
        std::chrono::seconds uptime;
        size_t sink_count;
    };

    Statistics get_statistics() const;
    void reset_statistics();

    // Flush all sinks
    void flush();

private:
    void write_to_sinks(const LogEntry& entry);
    bool should_log(LogLevel level) const;
    void increment_counters(LogLevel level);
};

// Debug macros for conditional compilation
#ifdef NDEBUG
    #define LOG_TRACE_ENABLED 0
    #define LOG_DEBUG_ENABLED 0
#else
    #define LOG_TRACE_ENABLED 1
    #define LOG_DEBUG_ENABLED 1
#endif

// Performance-aware logging macros
#if LOG_TRACE_ENABLED
    #define LOG_TRACE(logger, message) \
        if ((logger).get_level() <= LogLevel::TRACE) \
            (logger).trace(message, __FILE__, __LINE__, __FUNCTION__)
#else
    #define LOG_TRACE(logger, message) do {} while(0)
#endif

#if LOG_DEBUG_ENABLED
    #define LOG_DEBUG(logger, message) \
        if ((logger).get_level() <= LogLevel::DEBUG) \
            (logger).debug(message, __FILE__, __LINE__, __FUNCTION__)
#else
    #define LOG_DEBUG(logger, message) do {} while(0)
#endif

// Always enabled logs
#define LOG_INFO(logger, message) \
    if ((logger).get_level() <= LogLevel::INFO) \
        (logger).info(message, __FILE__, __LINE__, __FUNCTION__)

#define LOG_WARN(logger, message) \
    if ((logger).get_level() <= LogLevel::WARN) \
        (logger).warn(message, __FILE__, __LINE__, __FUNCTION__)

#define LOG_ERROR(logger, message) \
    if ((logger).get_level() <= LogLevel::ERROR) \
        (logger).error(message, __FILE__, __LINE__, __FUNCTION__)

#define LOG_FATAL(logger, message) \
    if ((logger).get_level() <= LogLevel::FATAL) \
        (logger).fatal(message, __FILE__, __LINE__, __FUNCTION__)

// Performance measurement macros
#define LOG_SCOPE(logger, name) \
    ScopeLogger scope_logger_##__LINE__(logger, name, __FILE__, __LINE__, __FUNCTION__)

#define LOG_PERF(logger, operation, duration_ms) \
    LOG_DEBUG(logger, "Performance: " + std::string(operation) + \
              " took " + std::to_string(duration_ms) + "ms")

#define LOG_COUNTER(logger, name, value) \
    LOG_TRACE(logger, "Counter [" + std::string(name) + "]: " + std::to_string(value))

// Helper class for scope-based logging
class ScopeLogger {
private:
    Logger& logger_;
    std::string name_;
    std::chrono::steady_clock::time_point start_time_;
    const char* file_;
    int line_;
    const char* function_;

public:
    ScopeLogger(Logger& logger, const std::string& name,
                const char* file, int line, const char* function)
        : logger_(logger), name_(name), file_(file), line_(line), function_(function) {
        LOG_TRACE(logger_, "Entering scope: " + name_);
        start_time_ = std::chrono::steady_clock::now();
    }

    ~ScopeLogger() {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_).count();

        LOG_TRACE(logger_, "Exiting scope: " + name_ +
                  " (duration: " + std::to_string(duration) + "μs)");
    }
};

// Log manager for global configuration
class LogManager {
private:
    static LogManager* instance_;
    std::mutex config_mutex_;
    LoggerConfig global_config_;
    std::string config_file_;
    std::atomic<bool> config_loaded_{false};
    std::thread config_watcher_thread_;
    std::atomic<bool> watching_{false};

    LogManager();

public:
    ~LogManager();

    static LogManager& get_instance();

    // Configuration management
    void load_config(const std::string& config_file = "");
    void save_config(const std::string& config_file = "");
    void apply_config(const LoggerConfig& config);

    const LoggerConfig& get_global_config() const { return global_config_; }

    // Auto-reload config on change
    void start_config_watcher();
    void stop_config_watcher();

    // Global operations
    void flush_all();
    void set_global_level(LogLevel level);
    void enable_all_loggers();
    void disable_all_loggers();

    // Statistics
    struct GlobalStatistics {
        size_t total_loggers;
        uint64_t total_logs;
        uint64_t total_errors;
        std::map<std::string, Logger::Statistics> logger_stats;
    };

    GlobalStatistics get_global_statistics() const;

private:
    void config_watcher_loop();
    LoggerConfig load_config_from_file(const std::string& file);
    LoggerConfig load_config_from_json(const std::string& json_str);
    std::string save_config_to_json(const LoggerConfig& config);
};
