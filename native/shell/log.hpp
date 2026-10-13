#pragma once
#include <string>

// Engine log lines keep the historical "LEVEL:SC64: message" form. Evidence
// checkers treat any line starting with "ERROR:" as a failed run, and parse
// "SC64 {json}" style telemetry lines anywhere in a line. Telemetry, window
// lifecycle and input traces are DEBUG: they are written only at log level
// debug (`--config=sc64.log_level=debug`), which the evidence tools request.
namespace sc64::shell::log {
enum class Level { debug, info, warning, error };
// "debug", "info", "warning" or "error" (the `sc64.log_level` values); false for anything else.
bool parse_level(const std::string& name, Level& level);
// Lines below the level are dropped. The default is info.
void set_level(Level level);
bool enabled(Level level);
void debug(const char* format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void info(const char* format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void warning(const char* format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void error(const char* format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
// Writes one already formatted line at `level`, used for structured telemetry (DEBUG).
void line(const std::string& text, Level level = Level::debug);
}
