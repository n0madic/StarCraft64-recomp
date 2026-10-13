// Log levels: DEBUG lines (telemetry, lifecycle, input traces) are dropped unless the level asks for them.
#include "log.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace sc64::shell;

#if !defined(_WIN32)
namespace {
// The text written to stdout while `body` runs (the Android build logs elsewhere; this test is host-only).
template <class Body> std::string capture(Body body) {
    std::fflush(stdout);
    char path[] = "/tmp/sc64-log-test-XXXXXX";
    const int file = mkstemp(path);
    assert(file >= 0);
    const int saved = dup(1);
    dup2(file, 1);
    body();
    std::fflush(stdout);
    dup2(saved, 1);
    close(saved);
    std::string text;
    char buffer[256];
    lseek(file, 0, SEEK_SET);
    for (ssize_t count; (count = read(file, buffer, sizeof buffer)) > 0;) text.append(buffer, std::size_t(count));
    close(file);
    unlink(path);
    return text;
}
}
#endif

int main() {
    log::Level level = log::Level::error;
    assert(log::parse_level("debug", level) && level == log::Level::debug);
    assert(log::parse_level("info", level) && level == log::Level::info);
    assert(log::parse_level("warning", level) && level == log::Level::warning);
    assert(log::parse_level("error", level) && level == log::Level::error);
    level = log::Level::warning;
    assert(!log::parse_level("verbose", level) && !log::parse_level("", level) && level == log::Level::warning);

#if !defined(_WIN32)
    // The default level is info: telemetry lines stay out of the way.
    assert(log::enabled(log::Level::info) && !log::enabled(log::Level::debug));
    auto text = capture([] {
        log::debug("hidden %d", 1);
        log::line("SC64 {\"fields\":1}");
        log::info("shown");
        log::warning("careful");
    });
    assert(text == "INFO:SC64: shown\nWARNING:SC64: careful\n");

    log::set_level(log::Level::debug);
    text = capture([] {
        log::debug("trace %d", 2);
        log::line("SC64 {\"fields\":1}");
        log::info("shown");
    });
    assert(text == "DEBUG:SC64: trace 2\nDEBUG:SC64: SC64 {\"fields\":1}\nINFO:SC64: shown\n");

    log::set_level(log::Level::warning);
    text = capture([] { log::info("quiet"); log::warning("w"); log::error("e"); });
    assert(text == "WARNING:SC64: w\nERROR:SC64: e\n");
#endif
    std::puts("log levels passed");
    return 0;
}
