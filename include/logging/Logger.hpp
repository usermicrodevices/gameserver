#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>

class Logger {
public:
    static void Initialize(const std::string& configPath = "");
    static std::shared_ptr<spdlog::logger> GetLogger(const std::string& name = "GameServer");

    template<typename... Args>
    static void Trace(const std::string& fmt, Args... args) {
        GetLogger()->trace(fmt, args...);
    }

    template<typename... Args>
    static void Debug(const std::string& fmt, Args... args) {
        GetLogger()->debug(fmt, args...);
    }

    template<typename... Args>
    static void Info(const std::string& fmt, Args... args) {
        GetLogger()->info(fmt, args...);
    }

    template<typename... Args>
    static void Warn(const std::string& fmt, Args... args) {
        GetLogger()->warn(fmt, args...);
    }

    template<typename... Args>
    static void Error(const std::string& fmt, Args... args) {
        GetLogger()->error(fmt, args...);
    }

    template<typename... Args>
    static void Critical(const std::string& fmt, Args... args) {
        GetLogger()->critical(fmt, args...);
    }

private:
    static std::shared_ptr<spdlog::logger> logger_;
    static std::string configPath_;
};
