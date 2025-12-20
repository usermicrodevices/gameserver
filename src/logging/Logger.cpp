#include "logging/Logger.hpp"
#include "config/ConfigManager.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>

std::shared_ptr<spdlog::logger> Logger::logger_;
std::string Logger::configPath_;

void Logger::Initialize(const std::string& configPath) {
    configPath_ = configPath;

    auto& config = ConfigManager::GetInstance();
    std::vector<spdlog::sink_ptr> sinks;

    if (config.GetConsoleOutput()) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::from_str(config.GetLogLevel()));
        sinks.push_back(console_sink);
    }

    if (!config.GetLogFilePath().empty()) {
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            config.GetLogFilePath(),
                                                                                config.GetMaxLogFileSize() * 1024 * 1024,
                                                                                config.GetMaxLogFiles()
        );
        file_sink->set_level(spdlog::level::from_str(config.GetLogLevel()));
        sinks.push_back(file_sink);
    }

    logger_ = std::make_shared<spdlog::logger>("GameServer", sinks.begin(), sinks.end());
    logger_->set_level(spdlog::level::from_str(config.GetLogLevel()));
    logger_->flush_on(spdlog::level::err);

    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);
}

std::shared_ptr<spdlog::logger> Logger::GetLogger(const std::string& name) {
    if (!logger_) {
        Initialize();
    }
    if (name == "GameServer") {
        return logger_;
    }
    return spdlog::get(name);
}
