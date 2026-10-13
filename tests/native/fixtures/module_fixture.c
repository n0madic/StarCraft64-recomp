/* Synthetic game module for test_game_module: built like the real one (pure C, shim headers,
 * -fexceptions, hidden visibility), with one defect per FIXTURE_* variant. No game data. */
#include "sc64_module.h"

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

static int entrypoint_calls;
static void fixture_entry(uint8_t* rdram, recomp_context* ctx) { (void)rdram; (void)ctx; ++entrypoint_calls; }
static void fixture_minimap(uint8_t* rdram, recomp_context* ctx) { (void)rdram; (void)ctx; entrypoint_calls += 100; }
static FuncEntry funcs[] = {{fixture_entry, 0, 4}};
static SectionTableEntry sections[] = {{0x1000, 0x80000400, 4, funcs, 1, NULL, 0, 0}};

#ifdef FIXTURE_BAD_ABI
#define INFO_ABI 99
#else
#define INFO_ABI SC64_MODULE_ABI
#endif
#ifdef FIXTURE_BAD_LAYOUT
#define INFO_SECTION_SIZE (sizeof(SectionTableEntry) + 8)
#else
#define INFO_SECTION_SIZE sizeof(SectionTableEntry)
#endif
#ifdef FIXTURE_BAD_ROM
#define INFO_ROM "0000000000000000000000000000000000000000"
#else
#define INFO_ROM "a36d91b5aac7bb95df0d7de5f9814f5270e25c2b"
#endif

static const Sc64ModuleInfo info = {
    INFO_ABI, sizeof(Sc64ModuleInfo), INFO_SECTION_SIZE, sizeof(FuncEntry), sizeof(recomp_context),
    0x80000400u, fixture_entry, sections, 1, fixture_minimap, INFO_ROM,
};

#ifndef FIXTURE_NO_INIT
EXPORT const Sc64ModuleInfo* sc64_module_init(const Sc64HostApi* host) {
#ifdef FIXTURE_REFUSE
    (void)host;
    return NULL;
#else
    if (!host || host->abi != SC64_MODULE_ABI || host->size != sizeof(Sc64HostApi)) return NULL;
    /* Every import the game needs must have been supplied. */
    if (!host->osCreateThread_recomp || !host->get_function || !host->sc64_ui_pick) return NULL;
    return &info;
#endif
}
#endif

/* Calls back into the host from module frames, like a guest function calling an OS wrapper
 * that throws ultramodern::thread_terminated; the exception must cross these frames. */
static volatile int sink;
static void guest_frame(void (*host_call)(void), int depth) {
    volatile char pad[48];
    pad[0] = (char)depth;
    sink += pad[0];
    if (depth > 0) guest_frame(host_call, depth - 1); else host_call();
    sink++;
}
EXPORT void fixture_call(void (*host_call)(void), int depth) { guest_frame(host_call, depth); }
EXPORT int fixture_entrypoint_calls(void) { return entrypoint_calls; }
