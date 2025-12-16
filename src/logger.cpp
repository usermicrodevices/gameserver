#include "logger.hpp"
#include <json/json.h>
#include <zlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <regex>

// Helper functions
namespace {
    std::string get_process_id() {
        static std::string pid = std::to_string(getpid());
        return pid;
    }

    std::string get_thread_id_str(std::thread::id id) {
        std::stringstream ss;
        ss << id;
        return ss.str();
    }

    std::string format_time(const std::chrono::system_clock::time_point& tp) {
        auto t = std::chrono::system_clock::to_time_t(tp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::string replace_pattern(const std::string& pattern, const LogEntry& entry) {
        std::string result = pattern;

        // Replace placeholders
        auto replace_all = [&](const std::string& from, const std::string& to) {
            size_t pos = 0;
            while ((pos = result.find(from, pos)) != std::string::npos) {
                result.replace(pos, from.length(), to);
                pos += to.length();
            }
        };

        replace_all("%Y", std::to_string(
            std::chrono::year_month_day(
                std::chrono::floor<std::chrono::days>(entry.timestamp)
            ).year()));
        replace_all("%m", std::to_string(
            std::chrono::year_month_day(
                std::chrono::floor<std::chrono::days>(entry.timestamp)
            ).month()));
        replace_all("%d", std::to_string(
            std::chrono::year_month_day(
                std::chrono::floor<std::chrono::days>(entry.timestamp)
            ).day()));

        replace_all("%H", "00"); // Placeholder - would need proper time formatting
        replace_all("%M", "00");
        replace_all("%S", "00");
        replace_all("%e", "000");

        replace_all("%l", LogLevelToString(entry.level));
        replace_all("%n", entry.logger_name);
        replace_all("%t", get_thread_id_str(entry.thread_id));
        replace_all("%p", entry.process_id);
        replace_all("%f", entry.file);
        replace_all("%F", fs::path(entry.file).filename().string());
        replace_all("%L", std::to_string(entry.line));
        replace_all("%M", entry.function);
        replace_all("%m", entry.message);

        return result;
    }
}

// LogEntry implementations
std::string LogEntry::to_string() const {
    std::stringstream ss;
    ss << format_time(timestamp) << " "
       << "[" << LogLevelToString(level) << "] "
       << "[" << logger_name << "] "
       << "[" << get_thread_id_str(thread_id) << "] "
       << file << ":" << line << " "
       << function << "() - "
       << message;
    return ss.str();
}

std::string LogEntry::to_json() const {
    Json::Value json;
    json["timestamp"] = format_time(timestamp);
    json["level"] = LogLevelToString(level);
    json["logger"] = logger_name;
    json["thread_id"] = get_thread_id_str(thread_id);
    json["process_id"] = process_id;
    json["file"] = file;
    json["line"] = line;
    json["function"] = function;
    json["message"] = message;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, json);
}

std::string LogEntry::to_csv() const {
    std::stringstream ss;
    ss << format_time(timestamp) << ","
       << LogLevelToString(level) << ","
       << logger_name << ","
       << get_thread_id_str(thread_id) << ","
       << process_id << ","
       << "\"" << file << "\"" << ","
       << line << ","
       << "\"" << function << "\"" << ","
       << "\"" << message << "\"";
    return ss.str();
}

// LogLevel conversions
const char* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        case LogLevel::OFF: return "OFF";
        default: return "UNKNOWN";
    }
}

LogLevel LogLevelFromString(const std::string& level) {
    std::string upper;
    std::transform(level.begin(), level.end(), std::back_inserter(upper), ::toupper);

    if (upper == "TRACE") return LogLevel::TRACE;
    if (upper == "DEBUG") return LogLevel::DEBUG;
    if (upper == "INFO") return LogLevel::INFO;
    if (upper == "WARN") return LogLevel::WARN;
    if (upper == "ERROR") return LogLevel::ERROR;
    if (upper == "FATAL") return LogLevel::FATAL;
    if (upper == "OFF") return LogLevel::OFF;

    return LogLevel::INFO; // Default
}

// ConsoleSink implementation
ConsoleSink::ConsoleSink(bool use_colors, bool use_timestamps)
    : use_colors_(use_colors), use_timestamps_(use_timestamps) {}

void ConsoleSink::write(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (use_colors_) {
        std::cout << get_color_code(entry.level);
    }

    std::cout << entry.to_string();

    if (use_colors_) {
        reset_color();
    }

    std::cout << std::endl;
}

void ConsoleSink::flush() {
    std::cout.flush();
}

std::string ConsoleSink::get_color_code(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "\033[37m";  // White
        case LogLevel::DEBUG: return "\033[36m";  // Cyan
        case LogLevel::INFO: return "\033[32m";   // Green
        case LogLevel::WARN: return "\033[33m";   // Yellow
        case LogLevel::ERROR: return "\033[31m";  // Red
        case LogLevel::FATAL: return "\033[35m";  // Magenta
        default: return "\033[0m";
    }
}

void ConsoleSink::reset_color() const {
    std::cout << "\033[0m";
}

// FileSink implementation
FileSink::FileSink(const std::string& file_path, size_t max_size,
                   size_t max_files, bool compress_old)
    : file_path_(file_path), max_size_(max_size), max_files_(max_files),
      compress_old_(compress_old), current_size_(0) {

    // Create directory if it doesn't exist
    fs::path dir = fs::path(file_path).parent_path();
    if (!dir.empty()) {
        fs::create_directories(dir);
    }

    // Extract base name and extension
    fs::path path_obj(file_path);
    base_name_ = path_obj.stem().string();
    extension_ = path_obj.extension().string();

    open_file();
}

FileSink::~FileSink() {
    close_file();
}

void FileSink::open_file() {
    file_stream_.open(file_path_, std::ios::app | std::ios::ate);
    if (file_stream_.is_open()) {
        current_size_ = file_stream_.tellp();
    } else {
        throw std::runtime_error("Failed to open log file: " + file_path_);
    }
}

void FileSink::close_file() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

void FileSink::write(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!file_stream_.is_open()) {
        open_file();
    }

    if (should_rotate()) {
        rotate_file();
    }

    std::string log_line = entry.to_string() + "\n";
    file_stream_ << log_line;
    file_stream_.flush();

    current_size_ += log_line.size();
}

void FileSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.flush();
    }
}

bool FileSink::should_rotate() const {
    return current_size_ >= max_size_;
}

void FileSink::rotate_file() {
    close_file();

    // Rename existing files
    for (int i = max_files_ - 1; i > 0; --i) {
        std::string old_path = get_rotated_path(i - 1);
        std::string new_path = get_rotated_path(i);

        if (fs::exists(old_path)) {
            if (fs::exists(new_path)) {
                fs::remove(new_path);
            }
            fs::rename(old_path, new_path);

            // Compress if needed
            if (compress_old_ && i == max_files_ - 1) {
                compress_file(new_path);
            }
        }
    }

    // Rename current file to .1
    if (fs::exists(file_path_)) {
        std::string first_rotated = get_rotated_path(0);
        fs::rename(file_path_, first_rotated);
    }

    // Cleanup old files
    cleanup_old_files();

    // Open new file
    open_file();
}

std::string FileSink::get_rotated_path(int index) const {
    std::stringstream ss;
    ss << fs::path(file_path_).parent_path().string() << "/"
       << base_name_ << "." << index << extension_;
    return ss.str();
}

void FileSink::cleanup_old_files() {
    for (size_t i = max_files_; i < max_files_ * 2; ++i) {
        std::string path = get_rotated_path(i);
        if (fs::exists(path)) {
            fs::remove(path);
        }
    }
}

void FileSink::compress_file(const std::string& path) {
    // Implement gzip compression
    std::ifstream src(path, std::ios::binary);
    if (!src.is_open()) return;

    std::string compressed_path = path + ".gz";
    gzFile dest = gzopen(compressed_path.c_str(), "wb");
    if (!dest) return;

    char buffer[8192];
    while (src.read(buffer, sizeof(buffer))) {
        gzwrite(dest, buffer, src.gcount());
    }
    if (src.gcount() > 0) {
        gzwrite(dest, buffer, src.gcount());
    }

    gzclose(dest);
    src.close();
    fs::remove(path);
}

size_t FileSink::get_written_count() const {
    // Count lines in file
    std::ifstream file(file_path_);
    return std::count(std::istreambuf_iterator<char>(file),
                      std::istreambuf_iterator<char>(), '\n');
}

// NetworkSink implementation
NetworkSink::NetworkSink(const std::string& host, uint16_t port)
    : host_(host), port_(port), socket_fd_(-1), connected_(false) {
    reconnect();
}

NetworkSink::~NetworkSink() {
    disconnect();
    watching_ = false;
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }
}

bool NetworkSink::connect() {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        return false;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
        close(socket_fd_);
        return false;
    }

    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd_);
        return false;
    }

    connected_ = true;
    return true;
}

void NetworkSink::disconnect() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    connected_ = false;
}

void NetworkSink::write(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_) {
        if (!connect()) {
            return;
        }
    }

    std::string log_data = entry.to_json() + "\n";

    ssize_t sent = send(socket_fd_, log_data.c_str(), log_data.size(), 0);
    if (sent < 0) {
        connected_ = false;
        reconnect_ = true;
    }
}

void NetworkSink::flush() {
    // Network sockets don't need flush
}

void NetworkSink::reconnect() {
    disconnect();
    connect();
}

void NetworkSink::reconnect_loop() {
    while (reconnect_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (reconnect_) {
            reconnect();
        }
    }
}

// AsyncSink implementation
AsyncSink::AsyncSink(std::unique_ptr<LogSink> sink, size_t max_queue_size)
    : inner_sink_(std::move(sink)), max_queue_size_(max_queue_size) {
    running_ = true;
    worker_thread_ = std::thread(&AsyncSink::worker_loop, this);
}

AsyncSink::~AsyncSink() {
    running_ = false;
    queue_cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    inner_sink_->flush();
}

void AsyncSink::write(const LogEntry& entry) {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    if (queue_.size() >= max_queue_size_) {
        dropped_++;
        return;
    }

    queue_.push(entry);
    lock.unlock();
    queue_cv_.notify_one();
}

void AsyncSink::flush() {
    // Wait for queue to empty
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this]() { return queue_.empty(); });
    inner_sink_->flush();
}

std::string AsyncSink::get_name() const {
    return "AsyncSink(" + inner_sink_->get_name() + ")";
}

size_t AsyncSink::get_queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
}

void AsyncSink::worker_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this]() { return !queue_.empty() || !running_; });

        while (!queue_.empty()) {
            LogEntry entry = std::move(queue_.front());
            queue_.pop();

            lock.unlock();
            inner_sink_->write(entry);
            lock.lock();
        }
    }
}

// FilterSink implementation
FilterSink::FilterSink(std::unique_ptr<LogSink> sink, LogLevel min_level,
                       LogLevel max_level, const std::string& pattern)
    : inner_sink_(std::move(sink)), min_level_(min_level),
      max_level_(max_level), pattern_(pattern) {}

void FilterSink::write(const LogEntry& entry) {
    if (should_log(entry)) {
        inner_sink_->write(entry);
    }
}

void FilterSink::flush() {
    inner_sink_->flush();
}

std::string FilterSink::get_name() const {
    return "FilterSink(" + inner_sink_->get_name() + ")";
}

bool FilterSink::should_log(const LogEntry& entry) const {
    if (entry.level < min_level_ || entry.level > max_level_) {
        return false;
    }

    if (!pattern_.empty()) {
        std::regex pattern_regex(pattern_);
        if (!std::regex_search(entry.message, pattern_regex)) {
            return false;
        }
    }

    // Check excluded files
    for (const auto& excluded : excluded_files_) {
        if (entry.file.find(excluded) != std::string::npos) {
            return false;
        }
    }

    // Check excluded functions
    for (const auto& excluded : excluded_functions_) {
        if (entry.function.find(excluded) != std::string::npos) {
            return false;
        }
    }

    return true;
}

// Logger implementation
std::mutex Logger::registry_mutex_;
std::map<std::string, Logger*> Logger::registry_;

Logger::Logger(const std::string& name, LogLevel level)
    : name_(name), level_(level), parent_(nullptr),
      creation_time_(std::chrono::steady_clock::now()) {

    // Register in global registry
    std::lock_guard<std::mutex> lock(registry_mutex_);
    registry_[name] = this;
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    registry_.erase(name_);
}

Logger& Logger::get_logger(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    auto it = registry_.find(name);
    if (it != registry_.end()) {
        return *it->second;
    }

    // Create new logger
    Logger* logger = new Logger(name);
    registry_[name] = logger;
    return *logger;
}

Logger& Logger::create_logger(const LoggerConfig& config) {
    std::lock_guard<std::mutex> lock(registry_mutex_);

    // Check if logger already exists
    auto it = registry_.find(config.name);
    if (it != registry_.end()) {
        delete it->second;
    }

    // Create new logger with configuration
    Logger* logger = new Logger(config.name, config.level);

    // Add sinks
    for (auto& sink : config.sinks) {
        logger->add_sink(std::move(sink));
    }

    registry_[config.name] = logger;
    return *logger;
}

void Logger::destroy_logger(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = registry_.find(name);
    if (it != registry_.end()) {
        delete it->second;
        registry_.erase(it);
    }
}

std::vector<std::string> Logger::get_logger_names() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    std::vector<std::string> names;
    for (const auto& pair : registry_) {
        names.push_back(pair.first);
    }
    return names;
}

void Logger::log(LogLevel level, const std::string& message,
                 const char* file, int line, const char* function) {
    if (!should_log(level)) {
        return;
    }

    LogEntry entry{
        std::chrono::system_clock::now(),
        level,
        name_,
        message,
        file ? file : "",
        line,
        function ? function : "",
        std::this_thread::get_id(),
        get_process_id()
    };

    write_to_sinks(entry);
    increment_counters(level);
}

void Logger::logf(LogLevel level, const char* format, ...) {
    if (!should_log(level)) {
        return;
    }

    va_list args;
    va_start(args, format);

    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    log(level, buffer);
}

void Logger::logv(LogLevel level, const char* format, va_list args) {
    if (!should_log(level)) {
        return;
    }

    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);

    log(level, buffer);
}

void Logger::trace(const std::string& message, const char* file,
                   int line, const char* function) {
    log(LogLevel::TRACE, message, file, line, function);
}

void Logger::debug(const std::string& message, const char* file,
                   int line, const char* function) {
    log(LogLevel::DEBUG, message, file, line, function);
}

void Logger::info(const std::string& message, const char* file,
                  int line, const char* function) {
    log(LogLevel::INFO, message, file, line, function);
}

void Logger::warn(const std::string& message, const char* file,
                  int line, const char* function) {
    log(LogLevel::WARN, message, file, line, function);
}

void Logger::error(const std::string& message, const char* file,
                   int line, const char* function) {
    log(LogLevel::ERROR, message, file, line, function);
}

void Logger::fatal(const std::string& message, const char* file,
                   int line, const char* function) {
    log(LogLevel::FATAL, message, file, line, function);
}

void Logger::add_sink(std::unique_ptr<LogSink> sink) {
    sinks_.push_back(std::move(sink));
}

void Logger::remove_sink(const std::string& sink_name) {
    sinks_.erase(std::remove_if(sinks_.begin(), sinks_.end(),
        [&](const std::unique_ptr<LogSink>& sink) {
            return sink->get_name() == sink_name;
        }), sinks_.end());
}

void Logger::clear_sinks() {
    sinks_.clear();
}

void Logger::write_to_sinks(const LogEntry& entry) {
    for (auto& sink : sinks_) {
        try {
            sink->write(entry);
        } catch (const std::exception& e) {
            error_count_++;
            // Don't throw from logger
        }
    }

    // Propagate to parent if configured
    if (parent_ && propagate) {
        parent_->write_to_sinks(entry);
    }
}

bool Logger::should_log(LogLevel level) const {
    return level >= level_;
}

void Logger::increment_counters(LogLevel level) {
    log_count_++;
    if (level >= LogLevel::ERROR) {
        error_count_++;
    }
}

Logger::Statistics Logger::get_statistics() const {
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - creation_time_);

    double logs_per_second = uptime.count() > 0 ?
        static_cast<double>(log_count_) / uptime.count() : 0.0;

    return {
        log_count_,
        error_count_,
        logs_per_second,
        uptime,
        sinks_.size()
    };
}

void Logger::reset_statistics() {
    log_count_ = 0;
    error_count_ = 0;
    creation_time_ = std::chrono::steady_clock::now();
}

void Logger::flush() {
    for (auto& sink : sinks_) {
        sink->flush();
    }
}

// LogManager implementation
LogManager* LogManager::instance_ = nullptr;

LogManager::LogManager() {
    // Default configuration
    global_config_.name = "root";
    global_config_.level = LogLevel::INFO;

    // Create logs directory
    fs::create_directories("logs");
}

LogManager::~LogManager() {
    stop_config_watcher();
    flush_all();
}

LogManager& LogManager::get_instance() {
    static std::mutex instance_mutex;
    std::lock_guard<std::mutex> lock(instance_mutex);

    if (!instance_) {
        instance_ = new LogManager();
    }
    return *instance_;
}

void LogManager::load_config(const std::string& config_file) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    if (config_file.empty() && config_file_.empty()) {
        // Try default locations
        std::vector<std::string> default_locations = {
            "logging.json",
            "config/logging.json",
            "/etc/game-network/logging.json"
        };

        for (const auto& location : default_locations) {
            if (fs::exists(location)) {
                config_file_ = location;
                break;
            }
        }
    } else if (!config_file.empty()) {
        config_file_ = config_file;
    }

    if (config_file_.empty()) {
        // Use default config
        apply_config(global_config_);
        return;
    }

    try {
        LoggerConfig config = load_config_from_file(config_file_);
        apply_config(config);
        config_loaded_ = true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load log config: " << e.what() << std::endl;
    }
}

void LogManager::save_config(const std::string& config_file) {
    std::string target_file = config_file.empty() ? config_file_ : config_file;
    if (target_file.empty()) {
        target_file = "logging.json";
    }

    std::ofstream file(target_file);
    if (file.is_open()) {
        file << save_config_to_json(global_config_);
        file.close();
    }
}

void LogManager::apply_config(const LoggerConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    global_config_ = config;

    // Apply to root logger
    Logger& root = Logger::get_logger("root");
    root.set_level(config.level);
    root.clear_sinks();

    // Add console sink
    if (config.console_options.enabled) {
        auto console_sink = std::make_unique<ConsoleSink>(
            config.console_options.colors,
            config.console_options.timestamps
        );

        if (config.async) {
            auto async_sink = std::make_unique<AsyncSink>(
                std::move(console_sink),
                config.async_queue_size
            );
            root.add_sink(std::move(async_sink));
        } else {
            root.add_sink(std::move(console_sink));
        }
    }

    // Add file sink
    auto file_sink = std::make_unique<FileSink>(
        config.file_options.path,
        config.file_options.max_size,
        config.file_options.max_files,
        config.file_options.compress
    );

    if (config.async) {
        auto async_sink = std::make_unique<AsyncSink>(
            std::move(file_sink),
            config.async_queue_size
        );
        root.add_sink(std::move(async_sink));
    } else {
        root.add_sink(std::move(file_sink));
    }

    // Add network sink
    if (config.network_options.enabled) {
        auto network_sink = std::make_unique<NetworkSink>(
            config.network_options.host,
            config.network_options.port
        );

        if (config.async) {
            auto async_sink = std::make_unique<AsyncSink>(
                std::move(network_sink),
                config.async_queue_size
            );
            root.add_sink(std::move(async_sink));
        } else {
            root.add_sink(std::move(network_sink));
        }
    }
}

void LogManager::start_config_watcher() {
    if (watching_ || config_file_.empty()) {
        return;
    }

    watching_ = true;
    config_watcher_thread_ = std::thread(&LogManager::config_watcher_loop, this);
}

void LogManager::stop_config_watcher() {
    watching_ = false;
    if (config_watcher_thread_.joinable()) {
        config_watcher_thread_.join();
    }
}

void LogManager::flush_all() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    for (auto& pair : Logger::registry_) {
        pair.second->flush();
    }
}

void LogManager::set_global_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    global_config_.level = level;

    // Apply to all loggers
    std::lock_guard<std::mutex> reg_lock(Logger::registry_mutex_);
    for (auto& pair : Logger::registry_) {
        pair.second->set_level(level);
    }
}

void LogManager::enable_all_loggers() {
    set_global_level(LogLevel::TRACE);
}

void LogManager::disable_all_loggers() {
    set_global_level(LogLevel::OFF);
}

LogManager::GlobalStatistics LogManager::get_global_statistics() const {
    GlobalStatistics stats;
    stats.total_logs = 0;
    stats.total_errors = 0;

    std::lock_guard<std::mutex> lock(Logger::registry_mutex_);
    stats.total_loggers = Logger::registry_.size();

    for (const auto& pair : Logger::registry_) {
        auto logger_stats = pair.second->get_statistics();
        stats.total_logs += logger_stats.total_logs;
        stats.total_errors += logger_stats.error_logs;
        stats.logger_stats[pair.first] = logger_stats;
    }

    return stats;
}

void LogManager::config_watcher_loop() {
    auto last_write_time = fs::last_write_time(config_file_);

    while (watching_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        try {
            auto current_write_time = fs::last_write_time(config_file_);
            if (current_write_time != last_write_time) {
                last_write_time = current_write_time;
                load_config(config_file_);
            }
        } catch (const std::exception& e) {
            // Log error (but we can't use logger here)
            std::cerr << "Config watcher error: " << e.what() << std::endl;
        }
    }
}

LoggerConfig LogManager::load_config_from_file(const std::string& file) {
    std::ifstream config_file(file);
    if (!config_file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + file);
    }

    std::stringstream buffer;
    buffer << config_file.rdbuf();

    return load_config_from_json(buffer.str());
}

LoggerConfig LogManager::load_config_from_json(const std::string& json_str) {
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errors;

    std::stringstream ss(json_str);
    if (!Json::parseFromStream(reader, ss, &root, &errors)) {
        throw std::runtime_error("Failed to parse JSON: " + errors);
    }

    LoggerConfig config;

    if (root.isMember("name")) config.name = root["name"].asString();
    if (root.isMember("level")) config.level = LogLevelFromString(root["level"].asString());
    if (root.isMember("async")) config.async = root["async"].asBool();
    if (root.isMember("async_queue_size")) config.async_queue_size = root["async_queue_size"].asUInt();
    if (root.isMember("pattern")) config.pattern = root["pattern"].asString();
    if (root.isMember("propagate")) config.propagate = root["propagate"].asBool();

    // File options
    if (root.isMember("file")) {
        const auto& file = root["file"];
        if (file.isMember("path")) config.file_options.path = file["path"].asString();
        if (file.isMember("max_size")) config.file_options.max_size = file["max_size"].asUInt64();
        if (file.isMember("max_files")) config.file_options.max_files = file["max_files"].asUInt();
        if (file.isMember("compress")) config.file_options.compress = file["compress"].asBool();
        if (file.isMember("daily_rotation")) config.file_options.daily_rotation = file["daily_rotation"].asBool();
    }

    // Network options
    if (root.isMember("network")) {
        const auto& network = root["network"];
        if (network.isMember("enabled")) config.network_options.enabled = network["enabled"].asBool();
        if (network.isMember("host")) config.network_options.host = network["host"].asString();
        if (network.isMember("port")) config.network_options.port = network["port"].asUInt();
    }

    // Console options
    if (root.isMember("console")) {
        const auto& console = root["console"];
        if (console.isMember("enabled")) config.console_options.enabled = console["enabled"].asBool();
        if (console.isMember("colors")) config.console_options.colors = console["colors"].asBool();
        if (console.isMember("timestamps")) config.console_options.timestamps = console["timestamps"].asBool();
    }

    return config;
}

std::string LogManager::save_config_to_json(const LoggerConfig& config) {
    Json::Value root;

    root["name"] = config.name;
    root["level"] = LogLevelToString(config.level);
    root["async"] = config.async;
    root["async_queue_size"] = static_cast<Json::UInt>(config.async_queue_size);
    root["pattern"] = config.pattern;
    root["propagate"] = config.propagate;

    // File options
    Json::Value file;
    file["path"] = config.file_options.path;
    file["max_size"] = static_cast<Json::UInt64>(config.file_options.max_size);
    file["max_files"] = static_cast<Json::UInt>(config.file_options.max_files);
    file["compress"] = config.file_options.compress;
    file["daily_rotation"] = config.file_options.daily_rotation;
    root["file"] = file;

    // Network options
    Json::Value network;
    network["enabled"] = config.network_options.enabled;
    network["host"] = config.network_options.host;
    network["port"] = config.network_options.port;
    root["network"] = network;

    // Console options
    Json::Value console;
    console["enabled"] = config.console_options.enabled;
    console["colors"] = config.console_options.colors;
    console["timestamps"] = config.console_options.timestamps;
    root["console"] = console;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    return Json::writeString(builder, root);
}
