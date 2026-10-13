#include "process.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace sc64::setup {
std::string quote_argument(const std::string& argument) {
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string::npos) return argument;
    std::string result = "\"";
    std::size_t backslashes = 0;
    for (char c : argument) {
        if (c == '\\') { ++backslashes; continue; }
        if (c == '"') { result.append(backslashes * 2 + 1, '\\'); result += '"'; }
        else { result.append(backslashes, '\\'); result += c; }
        backslashes = 0;
    }
    result.append(backslashes * 2, '\\');
    return result + '"';
}

std::string describe(const Command& command) {
    std::string text = quote_argument(command.program.string());
    for (const auto& argument : command.args) text += ' ' + quote_argument(argument);
    return text;
}

#if defined(_WIN32)
namespace {
std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), result.data(), size);
    return result;
}
}

int run_process(const Command& command) {
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE log = INVALID_HANDLE_VALUE;
    if (!command.log.empty()) {
        log = CreateFileW(command.log.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &inheritable, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open log " + command.log.string());
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = INVALID_HANDLE_VALUE;
    startup.hStdOutput = startup.hStdError = log;
    std::wstring line = wide(describe(command));
    // The block is the parent's environment with the overrides replacing same-named entries.
    std::wstring block;
    if (!command.env.empty()) {
        std::vector<std::wstring> overrides;
        for (const auto& [name, value] : command.env) overrides.push_back(wide(name) + L"=" + wide(value));
        wchar_t* inherited = GetEnvironmentStringsW();
        for (const wchar_t* entry = inherited; *entry; entry += wcslen(entry) + 1) {
            const std::wstring text(entry);
            const auto equals = text.find(L'=', 1);
            bool replaced = false;
            for (const auto& item : overrides)
                replaced |= equals != std::wstring::npos && _wcsnicmp(item.c_str(), text.c_str(), equals + 1) == 0;
            if (!replaced) block += text + L'\0';
        }
        FreeEnvironmentStringsW(inherited);
        for (const auto& item : overrides) block += item + L'\0';
        block += L'\0';
    }
    PROCESS_INFORMATION info{};
    const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | (block.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT), block.empty() ? nullptr : block.data(),
                                        nullptr, &startup, &info);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!started) throw std::runtime_error("Cannot start " + command.program.string() + " (error " + std::to_string(GetLastError()) + ")");
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD status = 1;
    GetExitCodeProcess(info.hProcess, &status);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return int(status);
}
#else
int run_process(const Command& command) {
    std::vector<std::string> words{command.program.string()};
    words.insert(words.end(), command.args.begin(), command.args.end());
    std::vector<char*> argv;
    for (auto& word : words) argv.push_back(word.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    const char* sink = command.log.empty() ? "/dev/null" : command.log.c_str();
    posix_spawn_file_actions_addopen(&actions, 1, sink, O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    std::vector<std::string> environment;
    for (char** entry = environ; entry && *entry; ++entry) {
        const std::string text(*entry);
        bool replaced = false;
        for (const auto& [name, value] : command.env) replaced |= text.starts_with(name + "=");
        if (!replaced) environment.push_back(text);
    }
    for (const auto& [name, value] : command.env) environment.push_back(name + "=" + value);
    std::vector<char*> envp;
    for (auto& entry : environment) envp.push_back(entry.data());
    envp.push_back(nullptr);
    pid_t pid = 0;
    const int error = posix_spawnp(&pid, words[0].c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (error) throw std::runtime_error("Cannot start " + words[0] + ": " + std::strerror(error));
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error("waitpid failed for " + words[0]);
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
}
#endif

BatchResult run_batch(const Runner& run, const std::vector<Command>& commands, unsigned jobs,
                      const std::function<void(std::size_t, std::size_t)>& progress) {
    BatchResult result;
    std::atomic<std::size_t> next{0}, done{0};
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::exception_ptr failure;
    auto worker = [&] {
        for (;;) {
            const std::size_t index = next.fetch_add(1);
            if (index >= commands.size() || stop) return;
            int status = 1;
            try {
                status = run(commands[index]);
            } catch (...) {
                std::lock_guard lock(mutex);
                if (!failure) failure = std::current_exception();
                stop = true;
                return;
            }
            std::lock_guard lock(mutex);
            if (status != 0 && result.ok) { result.ok = false; result.failed = index; result.status = status; stop = true; }
            else if (status != 0 && index < result.failed) { result.failed = index; result.status = status; }
            if (progress) progress(++done, commands.size());
        }
    };
    const unsigned count = std::max(1u, std::min<unsigned>(jobs, unsigned(std::max<std::size_t>(1, commands.size()))));
    std::vector<std::thread> threads;
    for (unsigned i = 1; i < count; ++i) threads.emplace_back(worker);
    worker();
    for (auto& thread : threads) thread.join();
    if (failure) std::rethrow_exception(failure);
    return result;
}

std::string run_capture(const Runner& run, Command command, const std::filesystem::path& scratch_log) {
    std::error_code ignored;
    std::filesystem::remove(scratch_log, ignored);
    command.log = scratch_log;
    int status = 1;
    try { status = run(command); } catch (const std::exception&) { return {}; }
    if (status != 0) return {};
    std::ifstream stream(scratch_log, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}
}
