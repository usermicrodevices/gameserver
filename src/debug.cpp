#include "debug.hpp"
#include <execinfo.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

// DebugCategory implementations
const char* DebugCategoryToString(DebugCategory category) {
    switch (category) {
        case DebugCategory::NETWORK: return "NETWORK";
        case DebugCategory::PROTOCOL: return "PROTOCOL";
        case DebugCategory::PYTHON: return "PYTHON";
        case DebugCategory::GAMEPLAY: return "GAMEPLAY";
        case DebugCategory::PERFORMANCE: return "PERFORMANCE";
        case DebugCategory::MEMORY: return "MEMORY";
        case DebugCategory::THREADING: return "THREADING";
        case DebugCategory::SECURITY: return "SECURITY";
        case DebugCategory::ALL: return "ALL";
        default: return "UNKNOWN";
    }
}

DebugCategory DebugCategoryFromString(const std::string& category) {
    static std::map<std::string, DebugCategory> mapping = {
        {"NETWORK", DebugCategory::NETWORK},
        {"PROTOCOL", DebugCategory::PROTOCOL},
        {"PYTHON", DebugCategory::PYTHON},
        {"GAMEPLAY", DebugCategory::GAMEPLAY},
        {"PERFORMANCE", DebugCategory::PERFORMANCE},
        {"MEMORY", DebugCategory::MEMORY},
        {"THREADING", DebugCategory::THREADING},
        {"SECURITY", DebugCategory::SECURITY},
        {"ALL", DebugCategory::ALL}
    };

    auto it = mapping.find(category);
    return it != mapping.end() ? it->second : DebugCategory::ALL;
}

// Breakpoint implementation
Breakpoint::Breakpoint(const std::string& name,
                       std::function<bool()> condition,
                       std::function<void()> action,
                       int max_hits)
    : name(name), condition(condition), action(action),
      enabled(true), hit_count(0), max_hits(max_hits) {}

// DebugMetric implementation
DebugMetric::DebugMetric(const std::string& name)
    : name(name), value(0), min_value(0), max_value(0),
      sample_count(0), start_time(std::chrono::steady_clock::now()) {}

void DebugMetric::update(double new_value) {
    double old_value = value.load();
    double current_min = min_value.load();
    double current_max = max_value.load();

    // Update value with exponential moving average
    double alpha = 0.1; // Smoothing factor
    double smoothed = alpha * new_value + (1 - alpha) * old_value;
    value.store(smoothed);

    // Update min/max
    if (new_value < current_min || sample_count == 0) {
        min_value.store(new_value);
    }
    if (new_value > current_max || sample_count == 0) {
        max_value.store(new_value);
    }

    sample_count++;
}

void DebugMetric::increment(double amount) {
    update(value.load() + amount);
}

void DebugMetric::reset() {
    value.store(0);
    min_value.store(0);
    max_value.store(0);
    sample_count.store(0);
    start_time = std::chrono::steady_clock::now();
}

double DebugMetric::get_average() const {
    return sample_count > 0 ? value.load() : 0.0;
}

double DebugMetric::get_rate() const {
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time).count();
    return duration > 0 ? sample_count.load() / static_cast<double>(duration) : 0.0;
}

std::string DebugMetric::to_string() const {
    std::stringstream ss;
    ss << name << ": "
       << "cur=" << std::fixed << std::setprecision(2) << value.load()
       << ", min=" << min_value.load()
       << ", max=" << max_value.load()
       << ", avg=" << get_average()
       << ", rate=" << get_rate() << "/s"
       << ", samples=" << sample_count.load();
    return ss.str();
}

// DebugProfiler implementation
DebugProfiler::DebugProfiler() : root_{"ROOT"} {
    root_.start_time = std::chrono::steady_clock::now();
    samples_by_name_["ROOT"] = &root_;
}

void DebugProfiler::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = true;
    root_.start_time = std::chrono::steady_clock::now();
    current_sample_ = &root_;
}

void DebugProfiler::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = false;
}

void DebugProfiler::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    root_.reset();
    samples_by_name_.clear();
    samples_by_name_["ROOT"] = &root_;
    current_sample_ = &root_;
}

void DebugProfiler::begin_sample(const std::string& name) {
    if (!enabled_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    ProfileSample* sample = find_or_create_sample(name, current_sample_);
    sample->start_time = std::chrono::steady_clock::now();
    sample->call_count++;
    current_sample_ = sample;
}

void DebugProfiler::end_sample() {
    if (!enabled_ || current_sample_ == &root_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - current_sample_->start_time);

    current_sample_->total_time += duration;
    current_sample_ = current_sample_->parent;
}

double DebugProfiler::ProfileSample::get_average_time() const {
    return call_count > 0 ? total_time.count() / static_cast<double>(call_count) : 0.0;
}

double DebugProfiler::ProfileSample::get_percentage() const {
    if (!parent || parent->total_time.count() == 0) return 100.0;
    return (total_time.count() * 100.0) / parent->total_time.count();
}

void DebugProfiler::ProfileSample::reset() {
    total_time = std::chrono::microseconds(0);
    call_count = 0;
    start_time = std::chrono::steady_clock::now();

    for (auto& child : children) {
        child.second->reset();
    }
}

DebugProfiler::ProfileSample* DebugProfiler::find_or_create_sample(
    const std::string& name, ProfileSample* parent) {

    // Check if sample already exists as a child
    auto it = parent->children.find(name);
    if (it != parent->children.end()) {
        return it->second;
    }

    // Create new sample
    ProfileSample* sample = new ProfileSample();
    sample->name = name;
    sample->parent = parent;
    sample->start_time = std::chrono::steady_clock::now();

    parent->children[name] = sample;
    samples_by_name_[name] = sample;

    return sample;
}

void DebugProfiler::generate_report_recursive(const ProfileSample& sample, int depth,
                                             std::stringstream& ss, double total_time) const {
    // Indent based on depth
    std::string indent(depth * 2, ' ');

    // Calculate percentages
    double sample_time = sample.total_time.count() / 1000.0; // Convert to ms
    double percent_of_total = (sample_time * 100.0) / (total_time / 1000.0);
    double percent_of_parent = sample.get_percentage();

    // Format line
    ss << indent << std::left << std::setw(40 - depth * 2) << sample.name
       << std::right << std::setw(8) << std::fixed << std::setprecision(2) << sample_time << "ms "
       << std::setw(6) << std::setprecision(1) << percent_of_parent << "% "
       << std::setw(6) << std::setprecision(1) << percent_of_total << "% "
       << std::setw(8) << sample.call_count << " calls "
       << std::setw(8) << std::setprecision(3) << sample.get_average_time() / 1000.0 << "ms avg\n";

    // Sort children by total time (descending)
    std::vector<const ProfileSample*> sorted_children;
    for (const auto& child : sample.children) {
        sorted_children.push_back(child.second);
    }

    std::sort(sorted_children.begin(), sorted_children.end(),
              [](const ProfileSample* a, const ProfileSample* b) {
                  return a->total_time > b->total_time;
              });

    // Recurse into children
    for (const auto* child : sorted_children) {
        generate_report_recursive(*child, depth + 1, ss, total_time);
    }
}

std::string DebugProfiler::generate_report() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!enabled_ || root_.call_count == 0) {
        return "Profiler not active or no samples collected.\n";
    }

    auto total_time = root_.total_time.count();

    std::stringstream ss;
    ss << "\n=== Performance Profiling Report ===\n";
    ss << "Total time: " << total_time / 1000.0 << "ms\n";
    ss << "Sample count: " << root_.call_count << "\n\n";

    ss << std::left << std::setw(40) << "Function"
       << std::right << std::setw(10) << "Time(ms)"
       << std::setw(8) << "%Parent"
       << std::setw(8) << "%Total"
       << std::setw(10) << "Calls"
       << std::setw(12) << "Avg(ms)\n";

    ss << std::string(86, '-') << "\n";

    // Generate report starting from root's children
    std::vector<const ProfileSample*> sorted_children;
    for (const auto& child : root_.children) {
        sorted_children.push_back(child.second);
    }

    std::sort(sorted_children.begin(), sorted_children.end(),
              [](const ProfileSample* a, const ProfileSample* b) {
                  return a->total_time > b->total_time;
              });

    for (const auto* child : sorted_children) {
        generate_report_recursive(*child, 0, ss, total_time);
    }

    return ss.str();
}

void DebugProfiler::save_report(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << generate_report();
        file.close();
    }
}

DebugProfiler::Scope::Scope(DebugProfiler& profiler, const std::string& name)
    : profiler_(profiler), name_(name), active_(profiler.is_enabled()) {
    if (active_) {
        profiler_.begin_sample(name_);
    }
}

DebugProfiler::Scope::~Scope() {
    if (active_) {
        profiler_.end_sample();
    }
}

// DebugMemoryTracker implementation
DebugMemoryTracker::DebugMemoryTracker() {
    // Default excluded types
    excluded_types_.insert("std::");
    excluded_types_.insert("__gnu_cxx::");
}

DebugMemoryTracker::~DebugMemoryTracker() {
    if (!allocations_.empty()) {
        std::cerr << "WARNING: Memory tracker destroyed with "
                  << allocations_.size() << " allocations still active!\n";
        save_report("memory_leaks_final.log");
    }
}

DebugMemoryTracker& DebugMemoryTracker::get_instance() {
    static DebugMemoryTracker instance;
    return instance;
}

void DebugMemoryTracker::track_allocation(void* ptr, size_t size, const std::string& type,
                                         const char* file, int line) {
    // Check if type should be excluded
    for (const auto& excluded : excluded_types_) {
        if (type.find(excluded) != std::string::npos) {
            return;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);

    Allocation alloc;
    alloc.ptr = ptr;
    alloc.size = size;
    alloc.type = type;
    alloc.file = file ? file : "";
    alloc.line = line;
    alloc.timestamp = std::chrono::steady_clock::now();

    if (track_stack_trace_) {
        alloc.stack_trace = get_stack_trace();
    }

    allocations_[ptr] = alloc;

    total_allocated_ += size;
    allocation_count_++;

    size_t current = total_allocated_.load();
    size_t peak = peak_allocated_.load();

    // Update peak if necessary
    while (current > peak && !peak_allocated_.compare_exchange_weak(peak, current)) {
        peak = peak_allocated_.load();
    }
}

void DebugMemoryTracker::track_deallocation(void* ptr) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = allocations_.find(ptr);
    if (it != allocations_.end()) {
        total_allocated_ -= it->second.size;
        deallocation_count_++;
        allocations_.erase(it);
    }
}

std::string DebugMemoryTracker::get_stack_trace(int max_depth) const {
    void* callstack[max_depth];
    int frames = backtrace(callstack, max_depth);
    char** symbols = backtrace_symbols(callstack, frames);

    if (!symbols) {
        return "Failed to get stack trace";
    }

    std::stringstream ss;
    for (int i = 1; i < frames; i++) { // Skip first frame (this function)
        Dl_info info;
        if (dladdr(callstack[i], &info) && info.dli_sname) {
            int status;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            ss << "  #" << i << " " << (status == 0 ? demangled : info.dli_sname)
               << " + " << (static_cast<char*>(callstack[i]) - static_cast<char*>(info.dli_saddr))
               << "\n";
            free(demangled);
        } else {
            ss << "  #" << i << " " << symbols[i] << "\n";
        }
    }

    free(symbols);
    return ss.str();
}

DebugMemoryTracker::MemoryStats DebugMemoryTracker::get_stats() const {
    MemoryStats stats;
    stats.current_allocated = total_allocated_.load();
    stats.peak_allocated = peak_allocated_.load();
    stats.total_allocations = allocation_count_.load();
    stats.total_deallocations = deallocation_count_.load();
    stats.memory_leak_count = allocations_.size();

    size_t leak_size = 0;
    for (const auto& pair : allocations_) {
        leak_size += pair.second.size;
    }
    stats.memory_leak_size = leak_size;

    return stats;
}

std::vector<DebugMemoryTracker::Allocation> DebugMemoryTracker::get_leaks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Allocation> leaks;

    for (const auto& pair : allocations_) {
        leaks.push_back(pair.second);
    }

    // Sort by size (largest first)
    std::sort(leaks.begin(), leaks.end(),
              [](const Allocation& a, const Allocation& b) {
                  return a.size > b.size;
              });

    return leaks;
}

std::string DebugMemoryTracker::generate_report() const {
    auto stats = get_stats();
    auto leaks = get_leaks();

    std::stringstream ss;
    ss << "\n=== Memory Debug Report ===\n";
    ss << "Current allocated: " << stats.current_allocated << " bytes\n";
    ss << "Peak allocated: " << stats.peak_allocated << " bytes\n";
    ss << "Total allocations: " << stats.total_allocations << "\n";
    ss << "Total deallocations: " << stats.total_deallocations << "\n";
    ss << "Memory leaks: " << stats.memory_leak_count << " ("
       << stats.memory_leak_size << " bytes)\n\n";

    if (!leaks.empty()) {
        ss << "Detected Memory Leaks:\n";
        ss << std::left << std::setw(16) << "Address"
           << std::setw(12) << "Size"
           << std::setw(30) << "Type"
           << std::setw(20) << "File:Line"
           << "Timestamp\n";

        ss << std::string(100, '-') << "\n";

        for (const auto& leak : leaks) {
            ss << std::left << std::setw(16) << leak.ptr
               << std::setw(12) << leak.size
               << std::setw(30) << (leak.type.size() > 28 ? leak.type.substr(0, 28) + ".." : leak.type)
               << std::setw(20) << (leak.file + ":" + std::to_string(leak.line))
               << std::put_time(std::localtime(&std::chrono::system_clock::to_time_t(leak.timestamp)),
                               "%Y-%m-%d %H:%M:%S")
               << "\n";

            if (!leak.stack_trace.empty()) {
                ss << "Stack trace:\n" << leak.stack_trace << "\n";
            }
        }
    }

    return ss.str();
}

void DebugMemoryTracker::save_report(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << generate_report();
        file.close();
    }
}

bool DebugMemoryTracker::check_memory_limit(size_t limit) const {
    return total_allocated_.load() > limit;
}

bool DebugMemoryTracker::check_leak_threshold(size_t threshold) const {
    size_t leak_size = 0;
    for (const auto& pair : allocations_) {
        leak_size += pair.second.size;
    }
    return leak_size > threshold;
}

// DebugSystem implementation
DebugSystem* DebugSystem::instance_ = nullptr;

DebugSystem::DebugSystem() : logger_(Logger::get_logger("debug")) {
    // Enable default categories
    for (auto category : config_.default_categories) {
        enabled_categories_.insert(category);
    }
}

DebugSystem& DebugSystem::get_instance() {
    static std::mutex instance_mutex;
    std::lock_guard<std::mutex> lock(instance_mutex);

    if (!instance_) {
        instance_ = new DebugSystem();
    }
    return *instance_;
}

void DebugSystem::initialize(const Config& config) {
    config_ = config;
    enabled_ = true;

    // Initialize logging for debug system
    LoggerConfig logger_config;
    logger_config.name = "debug";
    logger_config.level = LogLevel::DEBUG;
    logger_config.file_options.path = config.log_file;
    logger_config.file_options.max_size = 5 * 1024 * 1024; // 5MB
    logger_config.console_options.enabled = true;
    logger_config.console_options.colors = true;

    Logger::create_logger(logger_config);

    // Initialize subsystems based on config
    if (config.enable_profiling) {
        profiler_.start();
    }

    if (config.enable_memory_tracking) {
        start_memory_tracking();
    }

    logger_.info("Debug system initialized");
}

void DebugSystem::shutdown() {
    enabled_ = false;

    // Generate final reports
    if (config_.enable_profiling) {
        std::string profile_report = profiler_.generate_report();
        logger_.info("Final profiling report:\n" + profile_report);
        profiler_.save_report("logs/final_profile.log");
    }

    if (config_.enable_memory_tracking) {
        report_memory_leaks();
    }

    // Save debug metrics
    save_debug_report("logs/final_debug_report.log");

    logger_.info("Debug system shutdown complete");
}

void DebugSystem::enable_category(DebugCategory category) {
    enabled_categories_.insert(category);
}

void DebugSystem::disable_category(DebugCategory category) {
    enabled_categories_.erase(category);
}

bool DebugSystem::is_category_enabled(DebugCategory category) const {
    return enabled_ && (enabled_categories_.count(DebugCategory::ALL) > 0 ||
                       enabled_categories_.count(category) > 0);
}

void DebugSystem::enable_all_categories() {
    enabled_categories_.insert(DebugCategory::ALL);
}

void DebugSystem::disable_all_categories() {
    enabled_categories_.clear();
}

void DebugSystem::log(DebugCategory category, LogLevel level, const std::string& message,
                     const char* file, int line, const char* function) {
    if (!is_category_enabled(category)) {
        return;
    }

    std::string formatted = "[" + std::string(DebugCategoryToString(category)) + "] " + message;
    logger_.log(level, formatted, file, line, function);
}

void DebugSystem::logf(DebugCategory category, LogLevel level, const char* format, ...) {
    if (!is_category_enabled(category)) {
        return;
    }

    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::string formatted = "[" + std::string(DebugCategoryToString(category)) + "] " + buffer;
    logger_.log(level, formatted);
}

DebugMetric& DebugSystem::get_metric(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = metrics_.find(name);
    if (it == metrics_.end()) {
        it = metrics_.emplace(name, DebugMetric(name)).first;
    }
    return it->second;
}

void DebugSystem::update_metric(const std::string& name, double value) {
    get_metric(name).update(value);
}

void DebugSystem::increment_metric(const std::string& name, double amount) {
    get_metric(name).increment(amount);
}

std::map<std::string, DebugMetric> DebugSystem::get_all_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

void DebugSystem::reset_all_metrics() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : metrics_) {
        pair.second.reset();
    }
}

void DebugSystem::add_breakpoint(const std::string& name,
                                 std::function<bool()> condition,
                                 std::function<void()> action,
                                 int max_hits) {
    std::lock_guard<std::mutex> lock(mutex_);
    breakpoints_.emplace(name, Breakpoint(name, condition, action, max_hits));
}

void DebugSystem::remove_breakpoint(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    breakpoints_.erase(name);
}

void DebugSystem::enable_breakpoint(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = breakpoints_.find(name);
    if (it != breakpoints_.end()) {
        it->second.enabled = true;
    }
}

void DebugSystem::disable_breakpoint(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = breakpoints_.find(name);
    if (it != breakpoints_.end()) {
        it->second.enabled = false;
    }
}

void DebugSystem::check_breakpoints() {
    if (!config_.enable_breakpoints) return;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : breakpoints_) {
        Breakpoint& bp = pair.second;

        if (!bp.enabled) continue;
        if (bp.max_hits > 0 && bp.hit_count >= bp.max_hits) continue;

        if (bp.condition()) {
            bp.hit_count++;

            logger_.info("Breakpoint '" + bp.name + "' hit (count: " +
                        std::to_string(bp.hit_count) + ")");

            if (bp.action) {
                try {
                    bp.action();
                } catch (const std::exception& e) {
                    logger_.error("Breakpoint action failed: " + std::string(e.what()));
                }
            }
        }
    }
}

void DebugSystem::start_memory_tracking(bool track_stack) {
    auto& tracker = DebugMemoryTracker::get_instance();
    tracker.set_track_stack_trace(track_stack);
    logger_.info("Memory tracking started" +
                 std::string(track_stack ? " (with stack traces)" : ""));
}

void DebugSystem::stop_memory_tracking() {
    report_memory_leaks();
    logger_.info("Memory tracking stopped");
}

void DebugSystem::report_memory_leaks() {
    auto& tracker = DebugMemoryTracker::get_instance();
    auto leaks = tracker.get_leaks();

    if (!leaks.empty()) {
        std::string report = tracker.generate_report();
        logger_.error("Memory leaks detected:\n" + report);
        tracker.save_report("logs/memory_leaks.log");
    } else {
        logger_.info("No memory leaks detected");
    }
}

std::string DebugSystem::generate_debug_report() const {
    std::stringstream ss;

    ss << "\n=== Debug System Report ===\n\n";

    // Enabled categories
    ss << "Enabled Categories: ";
    if (enabled_categories_.count(DebugCategory::ALL)) {
        ss << "ALL";
    } else {
        for (auto category : enabled_categories_) {
            ss << DebugCategoryToString(category) << " ";
        }
    }
    ss << "\n\n";

    // Metrics
    ss << "Metrics:\n";
    auto all_metrics = get_all_metrics();
    for (const auto& pair : all_metrics) {
        ss << "  " << pair.second.to_string() << "\n";
    }
    ss << "\n";

    // Memory stats
    if (config_.enable_memory_tracking) {
        auto mem_stats = DebugMemoryTracker::get_instance().get_stats();
        ss << "Memory Statistics:\n";
        ss << "  Current: " << mem_stats.current_allocated << " bytes\n";
        ss << "  Peak: " << mem_stats.peak_allocated << " bytes\n";
        ss << "  Allocations: " << mem_stats.total_allocations << "\n";
        ss << "  Deallocations: " << mem_stats.total_deallocations << "\n";
        ss << "  Leaks: " << mem_stats.memory_leak_count << " ("
           << mem_stats.memory_leak_size << " bytes)\n\n";
    }

    // Breakpoints
    ss << "Breakpoints (" << breakpoints_.size() << "):\n";
    for (const auto& pair : breakpoints_) {
        const Breakpoint& bp = pair.second;
        ss << "  " << bp.name << ": "
           << (bp.enabled ? "enabled" : "disabled") << ", "
           << "hits=" << bp.hit_count
           << (bp.max_hits > 0 ? "/" + std::to_string(bp.max_hits) : "") << "\n";
    }

    return ss.str();
}

void DebugSystem::save_debug_report(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << generate_debug_report();
        file.close();
    }
}

void DebugSystem::assert_condition(bool condition, const std::string& message,
                                  const char* file, int line, const char* function) {
    if (!condition) {
        std::stringstream ss;
        ss << "Assertion failed: " << message << "\n"
           << "  at " << file << ":" << line << " in " << function << "\n";

        // Log the assertion failure
        Logger::get_logger("debug").error(ss.str());

        // Break into debugger if attached
        #ifdef __linux__
            raise(SIGTRAP);
        #elif defined(_WIN32)
            DebugBreak();
        #endif

        // Throw exception
        throw std::runtime_error(ss.str());
    }
}

void DebugSystem::update_config(const Config& new_config) {
    config_ = new_config;

    // Update subsystems
    if (config_.enable_profiling && !profiler_.is_enabled()) {
        profiler_.start();
    } else if (!config_.enable_profiling && profiler_.is_enabled()) {
        profiler_.stop();
    }

    logger_.info("Debug configuration updated");
}
