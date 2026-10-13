#include "log.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace sc64::shell::log {
namespace {
std::mutex mutex;
std::atomic<Level> threshold{Level::info};
#if defined(__ANDROID__)
constexpr const char* tag = "SC64";
#endif
std::string format(const char* pattern, va_list arguments) {
    va_list copy;
    va_copy(copy, arguments);
    const int size = std::vsnprintf(nullptr, 0, pattern, copy);
    va_end(copy);
    if (size <= 0) return {};
    std::vector<char> buffer(std::size_t(size) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), pattern, arguments);
    return {buffer.data(), std::size_t(size)};
}
void write(Level severity, const char* level, [[maybe_unused]] int priority, const std::string& message) {
    if (!enabled(severity)) return;
    std::lock_guard lock(mutex);
#if defined(__ANDROID__)
    __android_log_write(priority, tag, message.c_str());
#else
    std::fprintf(stdout, "%s:SC64: %s\n", level, message.c_str());
    std::fflush(stdout);
#endif
}
}

bool parse_level(const std::string& name, Level& level) {
    for (const auto& [text, value] : {std::pair{"debug", Level::debug}, {"info", Level::info},
                                      {"warning", Level::warning}, {"error", Level::error}}) {
        if (name == text) { level = value; return true; }
    }
    return false;
}
void set_level(Level level) { threshold = level; }
bool enabled(Level level) { return level >= threshold.load(); }

#if defined(__ANDROID__)
#define SC64_PRIORITY(name) ANDROID_LOG_##name
#else
#define SC64_PRIORITY(name) 0
#endif

void info(const char* pattern, ...) {
    va_list arguments;
    va_start(arguments, pattern);
    const auto message = format(pattern, arguments);
    va_end(arguments);
    write(Level::info, "INFO", SC64_PRIORITY(INFO), message);
}
void warning(const char* pattern, ...) {
    va_list arguments;
    va_start(arguments, pattern);
    const auto message = format(pattern, arguments);
    va_end(arguments);
    write(Level::warning, "WARNING", SC64_PRIORITY(WARN), message);
}
void error(const char* pattern, ...) {
    va_list arguments;
    va_start(arguments, pattern);
    const auto message = format(pattern, arguments);
    va_end(arguments);
    write(Level::error, "ERROR", SC64_PRIORITY(ERROR), message);
}
void debug(const char* pattern, ...) {
    va_list arguments;
    va_start(arguments, pattern);
    const auto message = format(pattern, arguments);
    va_end(arguments);
    write(Level::debug, "DEBUG", SC64_PRIORITY(DEBUG), message);
}
void line(const std::string& text, Level level) {
    switch (level) {
    case Level::debug: write(level, "DEBUG", SC64_PRIORITY(DEBUG), text); break;
    case Level::info: write(level, "INFO", SC64_PRIORITY(INFO), text); break;
    case Level::warning: write(level, "WARNING", SC64_PRIORITY(WARN), text); break;
    case Level::error: write(level, "ERROR", SC64_PRIORITY(ERROR), text); break;
    }
}
}
