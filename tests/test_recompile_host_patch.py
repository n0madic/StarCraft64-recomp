from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools/recompile"
sys.path.insert(0, str(TOOLS))
try:
    import patch_host_runtime as HOST
    import patch_runtime as DEVICE
    import patch_threadqueue_runtime as QUEUE
finally:
    sys.path.remove(str(TOOLS))
UPSTREAM = ROOT / "third_party/N64ModernRuntime"


class HostPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not (UPSTREAM / "ultramodern/src/threads.cpp").is_file():
            raise unittest.SkipTest("pinned runtime unavailable")
        cls.compiler = shutil.which("clang++") or shutil.which("c++")

    def compile_run(self, source_text: str, arguments: list[list[str]] | None = None) -> None:
        if self.compiler is None:
            self.skipTest("C++20 compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory) / "fixture.cpp", Path(directory) / "fixture"
            source.write_text(source_text)
            command = [self.compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-pthread"]
            for include in (UPSTREAM / "ultramodern/include", UPSTREAM / "thirdparty", UPSTREAM / "thirdparty/concurrentqueue"):
                command += ["-I", str(include)]
            result = subprocess.run(command + [str(source), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for args in arguments or [[]]:
                result = subprocess.run([str(binary)] + args, capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)

    def prepare_fixture(self, runtime: Path) -> None:
        paths = {**HOST.ORIGINAL_HASHES, "ultramodern/src/events.cpp": DEVICE.HASHES["ultramodern/src/events.cpp"]}
        for name, expected in paths.items():
            original = (UPSTREAM / name).read_bytes()
            self.assertEqual(hashlib.sha256(original).hexdigest(), expected, name)
            target = runtime / name
            target.parent.mkdir(parents=True, exist_ok=True)
            if name == "ultramodern/src/events.cpp":
                target.write_bytes(DEVICE.patch_events(original.decode()).encode('utf-8'))
            else:
                target.write_bytes(original)
        events = "ultramodern/src/events.cpp"
        marker = {"original_sha256": {events: paths[events]}, "patched_sha256": {events: hashlib.sha256((runtime / events).read_bytes()).hexdigest()}}
        (runtime / ".sc64-device-patch.json").write_text(json.dumps(marker))

    def apply_fixture(self, runtime: Path) -> None:
        with patch.object(sys, "argv", [str(TOOLS / "patch_host_runtime.py"), str(runtime)]), redirect_stdout(io.StringIO()):
            HOST.main()

    def fixture_directory(self):
        parent = ROOT / "local/reports/recompile"
        parent.mkdir(parents=True, exist_ok=True)
        return tempfile.TemporaryDirectory(dir=parent)

    def test_host_preparation_is_idempotent(self) -> None:
        with self.fixture_directory() as directory:
            runtime = Path(directory)
            self.prepare_fixture(runtime)
            self.apply_fixture(runtime)
            names = json.loads((runtime / "sc64-host-patch.json").read_text())["patched_sha256"]
            first = {name: (runtime / name).read_bytes() for name in names}
            self.assertEqual({name: hashlib.sha256(data).hexdigest() for name, data in first.items()}, names)
            self.apply_fixture(runtime)
            self.assertEqual(first, {name: (runtime / name).read_bytes() for name in names})

    def test_late_anchor_failure_preserves_all_runtime_files(self) -> None:
        with self.fixture_directory() as directory:
            runtime = Path(directory)
            self.prepare_fixture(runtime)
            first = {str(path.relative_to(runtime)): path.read_bytes() for path in runtime.rglob("*") if path.is_file()}
            original_replace = HOST.replace

            def fail_late(text: str, before: str, after: str) -> str:
                if before == "    ctx->r2 = 0x00000000; // Pretend the audio DMAs finish instantly":
                    raise ValueError("injected last-file source drift")
                return original_replace(text, before, after)

            with patch.object(HOST, "replace", side_effect=fail_late):
                with self.assertRaisesRegex(ValueError, "last-file"):
                    self.apply_fixture(runtime)
            self.assertEqual(first, {str(path.relative_to(runtime)): path.read_bytes() for path in runtime.rglob("*") if path.is_file()})

    def test_unrelated_private_change_is_preserved_and_rejected(self) -> None:
        with self.fixture_directory() as directory:
            runtime = Path(directory)
            self.prepare_fixture(runtime)
            target = runtime / "ultramodern/src/threads.cpp"
            target.write_text(target.read_text() + "\n// unrelated private change\n")
            changed = target.read_bytes()
            with self.assertRaisesRegex(SystemExit, "unrelated private"):
                self.apply_fixture(runtime)
            self.assertEqual(target.read_bytes(), changed)
            self.assertFalse((runtime / "sc64-host-patch.json").exists())

    def test_actual_queue_removal_head_middle_tail_missing_and_running(self) -> None:
        original = (UPSTREAM / "ultramodern/src/threadqueue.cpp").read_bytes()
        self.assertEqual(hashlib.sha256(original).hexdigest(), QUEUE.THREADQUEUE_SHA256)
        source = QUEUE.patch_threadqueue(original.decode())
        self.compile_run(source + r'''
#include <vector>
#include <stdexcept>
void require(bool ok) { if (!ok) throw std::runtime_error("thread queue removal regression"); }
int main() {
    std::vector<uint8_t> memory(4096);
    uint8_t* rdram = memory.data();
    const int32_t head_address = static_cast<int32_t>(0x80000000u);
    const int32_t a = static_cast<int32_t>(0x80000100u);
    const int32_t b = static_cast<int32_t>(0x80000200u);
    const int32_t c = static_cast<int32_t>(0x80000300u);
    auto* head = TO_PTR(PTR(OSThread), head_address);
    auto* first = TO_PTR(OSThread, a);
    auto* second = TO_PTR(OSThread, b);
    auto* third = TO_PTR(OSThread, c);
    *head = a; first->next = b; second->next = c; third->next = NULLPTR;
    require(ultramodern::thread_queue_remove(rdram, head_address, b));
    require(*head == a && first->next == c && third->next == NULLPTR);
    require(!ultramodern::thread_queue_remove(rdram, head_address, b));
    require(ultramodern::thread_queue_remove(rdram, head_address, c));
    require(*head == a && first->next == NULLPTR);
    require(ultramodern::thread_queue_remove(rdram, head_address, a));
    require(*head == NULLPTR);
    require(!ultramodern::thread_queue_remove(rdram, head_address, a));
    running_queue_impl = a; first->next = b; second->next = c; third->next = NULLPTR;
    require(ultramodern::thread_queue_remove(rdram, ultramodern::running_queue, b));
    require(running_queue_impl == a && first->next == c);
    require(ultramodern::thread_queue_remove(rdram, ultramodern::running_queue, a));
    require(running_queue_impl == c);
}
''')

    def test_actual_timer_shutdown_idle_future_and_interval(self) -> None:
        original = (UPSTREAM / "ultramodern/src/timer.cpp").read_bytes()
        self.assertEqual(hashlib.sha256(original).hexdigest(), QUEUE.TIMER_SHA256)
        source = QUEUE.patch_timers(original.decode())
        self.compile_run(source + r'''
#include <vector>
#include <string>
#include <stdexcept>
static std::atomic_uint32_t messages = 0;
void ultramodern::set_native_thread_name(const std::string&) {}
void ultramodern::set_native_thread_priority(ultramodern::ThreadPriority) {}
void ultramodern::enqueue_external_message_src(PTR(OSMesgQueue), OSMesg, bool, ultramodern::EventMessageSource) { ++messages; }
int main(int argc, char** argv) {
    using namespace std::chrono_literals;
    if (argc != 2) return 2;
    std::vector<uint8_t> memory(4096);
    ultramodern::init_timers(memory.data());
    const std::string mode(argv[1]);
    if (mode == "future") {
        osSetTimer(memory.data(), static_cast<int32_t>(0x80000040u), 3'600'000ull * counter_per_ms, 0, 0, 0);
    } else if (mode == "interval") {
        osSetTimer(memory.data(), static_cast<int32_t>(0x80000040u), 2 * counter_per_ms, 2 * counter_per_ms, 0, 0);
        const auto limit = std::chrono::steady_clock::now() + 1s;
        while (messages.load() < 2 && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(1ms);
        if (messages.load() < 2) throw std::runtime_error("interval timer did not fire");
    }
    std::this_thread::sleep_for(20ms);
    const auto before = std::chrono::steady_clock::now();
    sc64_join_timers();
    if (std::chrono::steady_clock::now() - before > 1s) throw std::runtime_error("timer shutdown did not wake bounded wait");
    if (timer_context.thread.joinable()) throw std::runtime_error("timer thread not joined");
    const auto stopped = messages.load();
    memory.clear(); memory.shrink_to_fit();
    std::this_thread::sleep_for(5ms);
    if (messages.load() != stopped) throw std::runtime_error("timer accessed memory after join");
    sc64_join_timers();
}
''', [["idle"], ["future"], ["interval"]])

    def test_actual_scheduler_exit_guards_and_live_handoff(self) -> None:
        with self.fixture_directory() as directory:
            runtime = Path(directory)
            self.prepare_fixture(runtime)
            self.apply_fixture(runtime)
            source = (runtime / "ultramodern/src/threads.cpp").read_text()
        start = source.index("void wait_for_resumed(")
        end = source.index("void ultramodern::run_next_thread_and_wait", start)
        functions = source[start:end]
        queue = QUEUE.patch_threadqueue((UPSTREAM / "ultramodern/src/threadqueue.cpp").read_text())
        fixture = r'''#include <atomic>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <ultramodern/ultramodern.hpp>
static std::mutex sc64_context_mutex;
std::atomic_bool exited = false;
PTR(OSThread) ultramodern::this_thread() { return static_cast<int32_t>(0x80000100u); }
extern "C" void osDestroyThread(RDRAM_ARG PTR(OSThread)) { throw std::runtime_error("unexpected destroy"); }
''' + queue + functions + r'''
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<typename Function> void terminated(Function call) {
    bool threw = false;
    try { call(); } catch (const ultramodern::thread_terminated&) { threw = true; }
    require(threw, "shutdown did not stop scheduler");
}
int main() {
    using namespace std::chrono_literals;
    std::vector<uint8_t> memory(4096);
    uint8_t* rdram = memory.data();
    const int32_t first_address = static_cast<int32_t>(0x80000100u);
    auto* first = TO_PTR(OSThread, first_address);
    UltraThreadContext live;
    first->context = &live;
    first->next = NULLPTR;
    resume_thread(first);
    require(live.running.tryWait(), "resume did not signal live context");
    running_queue_impl = first_address;
    run_next_thread(rdram);
    require(running_queue_impl == NULLPTR && live.running.tryWait(), "queue handoff did not signal selected thread");
    first->context = nullptr;
    bool rejected = false;
    try { resume_thread(first); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "destroyed context must be rejected rather than dereferenced");
    running_queue_impl = first_address;
    exited.store(true);
    terminated([&] { run_next_thread(rdram); });
    require(running_queue_impl == first_address, "shutdown must stop before popping stale queue entry");
    terminated([&] { resume_thread(first); });
    const auto before = std::chrono::steady_clock::now();
    terminated([&] { wait_for_resumed(rdram, &live); });
    require(std::chrono::steady_clock::now() - before < 1s, "blocked context did not stop within bounded wait");
    exited.store(false);
    std::atomic_bool requested = false, stopped = false;
    sc64_context_mutex.lock();
    std::thread concurrent([&] {
        requested.store(true);
        try { resume_thread(first); } catch (const ultramodern::thread_terminated&) { stopped.store(true); }
    });
    while (!requested.load()) std::this_thread::yield();
    exited.store(true);
    sc64_context_mutex.unlock();
    concurrent.join();
    require(stopped.load(), "resume must observe shutdown after acquiring context lock");
}
'''
        self.compile_run(fixture)

    def test_actual_cleaner_waits_until_every_context_is_joined(self) -> None:
        with self.fixture_directory() as directory:
            runtime = Path(directory)
            self.prepare_fixture(runtime)
            self.apply_fixture(runtime)
            source = (runtime / "ultramodern/src/threads.cpp").read_text()
        start = source.index("static std::thread thread_cleaner_thread;")
        cleaner = source[start:]
        fixture = r'''#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <semaphore>
#include <vector>
#include <stdexcept>
#include <ultramodern/ultramodern.hpp>
#include "blockingconcurrentqueue.h"
static std::mutex sc64_context_mutex;
static std::condition_variable sc64_context_changed;
static std::unordered_set<UltraThreadContext*> sc64_live_contexts;
static size_t sc64_pending_cleanup = 0;
std::atomic_bool exited = true;
''' + cleaner + r'''
int main() {
    using namespace std::chrono_literals;
    std::counting_semaphore<10> start(0), tail(0);
    std::atomic_int enqueued = 0, finished = 0;
    std::atomic_bool join_finished = false;
    ultramodern::init_thread_cleanup();
    std::vector<UltraThreadContext*> contexts;
    for (int i = 0; i < 3; ++i) {
        auto* context = new UltraThreadContext;
        contexts.push_back(context);
        {
            std::lock_guard lock(sc64_context_mutex);
            sc64_live_contexts.insert(context);
        }
        context->host_thread = std::thread([&, context] {
            start.acquire();
            ultramodern::cleanup_thread(context);
            ++enqueued;
            tail.acquire();
            ++finished;
        });
    }
    start.release(3);
    const auto limit = std::chrono::steady_clock::now() + 1s;
    while (enqueued.load() < 3 && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(1ms);
    if (enqueued.load() != 3) throw std::runtime_error("cleanup producers did not finish enqueueing");
    std::thread joining([&] { ultramodern::join_thread_cleaner_thread(); join_finished.store(true); });
    std::this_thread::sleep_for(20ms);
    if (join_finished.load()) throw std::runtime_error("cleaner returned before host thread tails finished");
    tail.release(3);
    joining.join();
    if (!join_finished.load() || finished.load() != 3 || sc64_pending_cleanup != 0 || !sc64_live_contexts.empty()) {
        throw std::runtime_error("cross-producer cleanup sentinel overtook an unjoined context");
    }
}
'''
        self.compile_run(fixture)


if __name__ == "__main__":
    unittest.main()
