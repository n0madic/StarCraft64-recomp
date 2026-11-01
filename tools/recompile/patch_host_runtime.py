#!/usr/bin/env python3
"""Apply the capture/scheduling bridge after patch_runtime.py, privately.

This targets the pinned runtime, not guest bytes. Reapply device preparation
before this script when regenerating the private dependency tree.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from private_paths import require_local_or

from patch_runtime import patch_events
from patch_vi_runtime import patch_vi_events
from patch_threadqueue_runtime import patch_threadqueue, patch_timers, THREADQUEUE_SHA256, TIMER_SHA256

ORIGINAL_HASHES = {
    "librecomp/src/recomp.cpp": "07a6cff619c458a6d07aeb9410875e469f0bf9e7ffa31b3825914aca5b262f20",
    "librecomp/src/ai.cpp": "e795b226137f57737236b24e1c6498f16fd614038042f2d1eee48037c2dacbe7",
    "librecomp/src/vi.cpp": "5d7e7f0d9a610cc2eee9323cd407a741c35aebac8cce9100422f0128bc1549d0",
    "ultramodern/src/threads.cpp": "a2fef03027eece8d5ae6346b4f8695ed3fb2b03217069e6b8ef4a80995771468",
    "ultramodern/src/mesgqueue.cpp": "b504bd7e4290189e0e7e7b522486cc4b9ec22ae54f73297d4fa2c0e3f1575045",
    "ultramodern/src/threadqueue.cpp": THREADQUEUE_SHA256,
    "ultramodern/src/timer.cpp": TIMER_SHA256,
}


def replace(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise ValueError(f"Runtime source drift at {before[:90]!r}")
    return text.replace(before, after, 1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    runtime = require_local_or(sys.exit, args.runtime, root=root, what="Patched dependencies")
    marker = runtime / "sc64-host-patch.json"
    previous = {}
    if marker.exists():
        manifest = json.loads(marker.read_text())
        previous = manifest.get("patched_sha256", manifest)
    upstream = root / "third_party/N64ModernRuntime"
    device = json.loads((runtime / ".sc64-device-patch.json").read_text())
    originals = {}
    for name, digest in {**ORIGINAL_HASHES, "ultramodern/src/events.cpp": device["original_sha256"]["ultramodern/src/events.cpp"]}.items():
        data = (upstream / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f"Upstream source drift: {name}")
        text = data.decode()
        if name == "ultramodern/src/events.cpp":
            text = patch_events(text)
            if hashlib.sha256(text.encode()).hexdigest() != device["patched_sha256"][name]:
                raise SystemExit("Device event bridge differs from its manifest")
        base_hash = hashlib.sha256(text.encode()).hexdigest()
        current_hash = hashlib.sha256((runtime / name).read_bytes()).hexdigest()
        if current_hash not in (base_hash, previous.get(name)):
            raise SystemExit(f"Refusing unrelated private runtime changes: {name}")
        originals[name] = text
    changes = {}

    path = "librecomp/src/recomp.cpp"
    text = originals[path]
    text = replace(text, "load_overlays(0x1000, (int32_t)entrypoint, 1024 * 1024);",
                   "load_overlays(0x1000, (int32_t)entrypoint, 0xD5640);")
    # Retain identity until all guest threads have left their callbacks.
    text = replace(text, "    current_game.reset();", "    // SC64: retained until process shutdown; callbacks may still finish.")
    text = 'extern "C" void sc64_join_timers();\n' + text
    text = replace(text, '    ultramodern::join_saving_thread();', '    ultramodern::join_saving_thread();\n    sc64_join_timers();')
    text = replace(text, '''    graphics_shutdown_ready.signal();

    game_thread.join();
    ultramodern::join_event_threads();
    ultramodern::join_thread_cleaner_thread();''', '''    game_thread.join();
    ultramodern::join_thread_cleaner_thread();
    graphics_shutdown_ready.signal();
    ultramodern::join_event_threads();''')
    changes[path] = text

    path = "ultramodern/src/events.cpp"
    text = originals[path]
    # Take the snapshot while the guest submitting thread owns execution.
    text = replace(text, "void ultramodern::submit_rsp_task(RDRAM_ARG PTR(OSTask) task_) {",
        'extern "C" void sc64_capture_submit(uint8_t*, const OSTask*);\n\n'
        "void ultramodern::submit_rsp_task(RDRAM_ARG PTR(OSTask) task_) {")
    text = replace(text, "        events_context.action_queue.enqueue(SpTaskAction{ *task });",
        "        sc64_capture_submit(rdram, task);\n        events_context.action_queue.enqueue(SpTaskAction{ *task });")
    # Capture backend has no RSP/DP memory effects; completion follows capture.
    text = replace(text, "                sp_complete();\n                ultramodern::measure_input_latency();",
        "                ultramodern::measure_input_latency();")
    text = replace(text, "                dp_complete();", "                sp_complete();\n                dp_complete();")
    text = replace(text, 'extern moodycamel::LightweightSemaphore graphics_shutdown_ready;',
        'extern moodycamel::LightweightSemaphore graphics_shutdown_ready;\nstatic std::atomic_bool sc64_graphics_input_closed = false;')
    text = replace(text, '''    while (!exited) {
        // Try to pull an action from the queue''', '''    while (!sc64_graphics_input_closed.load() || events_context.action_queue.size_approx() != 0) {
        // All guest producers join before input closes. Drain every task.
        // Try to pull an action from the queue''')
    text = replace(text, '''    events_context.sp.gfx_thread.join();
    events_context.vi.thread.join();''', '''    events_context.vi.thread.join();
    sc64_graphics_input_closed.store(true);
    events_context.sp.gfx_thread.join();''')
    text = replace(text, "        events_context.vi.update_vi();", """        {
            std::lock_guard lock(events_context.message_mutex);
            events_context.vi.update_vi();
            events_context.vi.field ^= 1;
        }""")
    text += '''
// SC64: osCreateViManager must establish the default black NTSC context before
// the original game waits on osViSetEvent. See libultra src/io/vi.c.
extern "C" void sc64_initialize_vi() {
    std::lock_guard lock(events_context.message_mutex);
    auto* next = events_context.vi.get_next_state();
    if (next->mode == nullptr) {
        next->mode = &dummy_mode;
        next->control = dummy_mode.comRegs.ctrl;
        next->state |= VI_STATE_BLACK;
        next->framebuffer = static_cast<int32_t>(0x80000000);
    }
}
'''
    changes[path] = patch_vi_events(text)

    path = "librecomp/src/vi.cpp"
    text = originals[path]
    text = replace(text, 'extern "C" void osCreateViManager_recomp',
        'extern "C" void sc64_initialize_vi();\n\nextern "C" void osCreateViManager_recomp')
    text = replace(text, 'extern "C" void osCreateViManager_recomp(uint8_t* rdram, recomp_context* ctx) {\n    ;\n}',
        'extern "C" void osCreateViManager_recomp(uint8_t* rdram, recomp_context* ctx) {\n    sc64_initialize_vi();\n}')
    changes[path] = text

    path = "ultramodern/src/threads.cpp"
    text = originals[path]
    text = replace(text, '#include <string>', '#include <string>\n#include <mutex>\n#include <condition_variable>\n#include <unordered_set>')
    text = replace(text, "static ultramodern::threads::callbacks_t threads_callbacks;", """static ultramodern::threads::callbacks_t threads_callbacks;
extern std::atomic_bool exited;
static std::mutex sc64_context_mutex;
static std::condition_variable sc64_context_changed;
static std::unordered_set<UltraThreadContext*> sc64_live_contexts;
static size_t sc64_pending_cleanup = 0;""")
    text = replace(text, "    thread_context->running.wait();", """    while (!thread_context->running.wait(20'000)) {
        if (exited.load()) throw ultramodern::thread_terminated{};
    }
    if (exited.load()) throw ultramodern::thread_terminated{};""")
    text = replace(text, 'void resume_thread(OSThread* t) {', '''void resume_thread(OSThread* t) {
    std::lock_guard lock(sc64_context_mutex);
    if (exited.load()) throw ultramodern::thread_terminated{};
    if (t->context == nullptr) throw std::runtime_error("Cannot resume a destroyed guest thread");''')
    text = replace(text, 'void run_next_thread(RDRAM_ARG1) {', '''void run_next_thread(RDRAM_ARG1) {
    std::lock_guard lock(sc64_context_mutex);
    if (exited.load()) throw ultramodern::thread_terminated{};''')
    text = replace(text, '    to_run->context->running.signal();', '''    if (to_run->context == nullptr) throw std::runtime_error("Queued guest thread has no live context");
    to_run->context->running.signal();''')
    text = replace(text, "    if (self->context == thread_context) {\n        debug_printf(\"[Thread] Thread started:",
        "    if (!exited.load() && self->context == thread_context) {\n        debug_printf(\"[Thread] Thread started:")
    text = replace(text, "        run_next_thread(PASS_RDRAM1);\n    }\n\n    // Dispose", "        if (!exited.load()) run_next_thread(PASS_RDRAM1);\n    }\n\n    // Dispose")
    text = replace(text, '''    if (self->context == thread_context) {
        self->context = nullptr;
        if (!exited.load()) run_next_thread(PASS_RDRAM1);
    }''', '''    bool schedule_next = false;
    {
        std::lock_guard lock(sc64_context_mutex);
        if (self->context == thread_context) {
            self->context = nullptr;
            schedule_next = !exited.load();
        }
    }
    if (schedule_next) {
        try { run_next_thread(PASS_RDRAM1); }
        catch (const ultramodern::thread_terminated&) {}
    }''')
    text = replace(text, "        //throw ultramodern::thread_terminated{};", "        throw ultramodern::thread_terminated{};")
    text = replace(text, '''    // Otherwise if the thread isn't stopped, remove it from its currrent queue.,''', '''    std::lock_guard lock(sc64_context_mutex);
    if (exited.load()) throw ultramodern::thread_terminated{};
    // Otherwise if the thread isn't stopped, remove it from its currrent queue.,''')
    text = replace(text, "    t->context = context;", """    t->context = context;
    {
        std::lock_guard lock(sc64_context_mutex);
        sc64_live_contexts.insert(context);
    }""")
    text = replace(text, "    while (!exited) {\n        UltraThreadContext* to_delete;", "    while (true) {\n        UltraThreadContext* to_delete;")
    text = replace(text, '            debug_printf("[Cleanup] Deleting thread context', '            if (to_delete == nullptr) return;\n            debug_printf("[Cleanup] Deleting thread context')
    text = replace(text, "            delete to_delete;", """            delete to_delete;
            {
                std::lock_guard lock(sc64_context_mutex);
                --sc64_pending_cleanup;
                sc64_context_changed.notify_all();
            }""")
    text = replace(text, "    deleted_threads.enqueue(cur_context);", """    std::lock_guard lock(sc64_context_mutex);
    ++sc64_pending_cleanup;
    deleted_threads.enqueue(cur_context);
    sc64_live_contexts.erase(cur_context);
    sc64_context_changed.notify_all();""")
    text = replace(text, "    thread_cleaner_thread.join();", """    {
        std::unique_lock lock(sc64_context_mutex);
        sc64_context_changed.wait(lock, [] { return sc64_live_contexts.empty() && sc64_pending_cleanup == 0; });
    }
    deleted_threads.enqueue(nullptr);
    thread_cleaner_thread.join();""")
    changes[path] = text

    path = "ultramodern/src/mesgqueue.cpp"
    text = originals[path]
    text = replace(text, "static moodycamel::BlockingConcurrentQueue<QueuedMessage> external_messages {};",
        "extern std::atomic_bool exited;\nstatic moodycamel::BlockingConcurrentQueue<QueuedMessage> external_messages {};")
    text = replace(text, "    external_messages.wait_dequeue(to_send);", """    while (!external_messages.wait_dequeue_timed(to_send, std::chrono::milliseconds(20))) {
        if (exited.load()) throw ultramodern::thread_terminated{};
    }
    if (exited.load()) throw ultramodern::thread_terminated{};""")
    text = replace(text, "    QueuedMessage to_send;\n    if (external_messages.wait_dequeue_timed", "    if (exited.load()) throw ultramodern::thread_terminated{};\n    QueuedMessage to_send;\n    if (external_messages.wait_dequeue_timed")
    changes[path] = text

    path = "librecomp/src/ai.cpp"
    text = originals[path]
    text = replace(text, 'extern "C" void osAiGetStatus_recomp',
                   'extern "C" uint32_t sc64_ai_status(uint8_t*);\n\nextern "C" void osAiGetStatus_recomp')
    text = replace(text, "    ctx->r2 = 0x00000000; // Pretend the audio DMAs finish instantly",
                   "    ctx->r2 = sc64_ai_status(rdram);")
    changes[path] = text

    changes["ultramodern/src/threadqueue.cpp"] = patch_threadqueue(originals["ultramodern/src/threadqueue.cpp"])
    changes["ultramodern/src/timer.cpp"] = patch_timers(originals["ultramodern/src/timer.cpp"])

    # Validate all anchors before changing any file.
    for name, text in changes.items():
        (runtime / name).write_bytes(text.encode('utf-8'))
    manifest = {name: hashlib.sha256((runtime / name).read_bytes()).hexdigest() for name in changes}
    marker.write_text(json.dumps({"patched_sha256": manifest,
        "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}, indent=2) + "\n")
    print("Applied private host bridge")


if __name__ == "__main__":
    main()
