#pragma once

// heliasdream-vk rc2: the performance capture (F11, or `bbtest snap "note"`).
//
// A rolling in-memory record of the last ~30 s, written out only when asked:
//   - every presented frame: when it went, the interval to the one before
//     (what MangoHud times), flip-to-present, frames replaced unshown, the
//     command processor's waits for the presenter (the frame-latency limit);
//   - every second: the GPU busy share (host/gpu_busy.cpp), the area (the map
//     block the player is in), each thread's CPU time (Linux: /proc);
//   - the GPU sampler's frames (host/gpu.cpp: one frame every ~10 s timed pass
//     by pass, skipped when the last frame was over budget), FSR's and the
//     sharpening's own times (host/pass_timer.h);
//   - post at render's coverage: frames, moved draws, fallbacks and why, what
//     closed the window (host/render.cpp);
//   - the log's last 30 s (the 300-flip present/gpu busy/gpu idle lines);
//   - the settings and every HELIAS_ environment variable.
// Recording is a few stores a frame under an uncontended lock (the presenter's
// thread). A capture copies the rings and hands them to a thread of its own,
// which formats and writes the file: no frame waits on the disk.
//
// Where: $HELIAS_SNAP_DIR (bbtest start sets it to the session's snaps/),
// else ./snaps beside heliasdream. Requests: F11, or a file `snap.request` in that
// folder (its text is the note), which `bbtest snap` writes.
// HELIAS_SNAP=0 turns all of it off (debugging).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perf {

bool enabled();
// Starts the capture thread (once). Safe to call more than once.
void start();
// The presenter, after each present returned. `arrived_ns`: the flip's
// arrival (0: unknown). Totals, not deltas.
// rc3: where the flip-to-present went - the frame's wait to be recorded
// (arrival to its blit recorded: the presenter busy with the frame before,
// the acquire, the lock), the blit's submit (recorded to on the queue), the
// pacer's wait and vkQueuePresentKHR; -1 unknown. `priority`: the blit went
// by the priority submit (gpu.h).
struct PresentSplit {
    std::int64_t record_ns = -1, submit_ns = -1, pace_ns = -1, present_ns = -1;
    bool priority = false;
};
void note_present(std::int64_t now_ns, std::int64_t arrived_ns, std::uint64_t dropped_total, std::uint64_t cp_waits_total,
                  std::uint64_t cp_wait_ns_total, const PresentSplit& split = {});
// rc3: a section of counters written into every snapshot, from any module
// (the pacer, the occlusion resolve, the walking vertex shaders, the GPU's
// idle time with work queued). `fn` runs on the capture thread: it reads
// atomics only. Register once (static init or start).
using SectionFn = std::string (*)();
void add_section(const char* title, SectionFn fn);
// The last present interval (ns; 0 before two presents) - the GPU sampler
// skips a frame after one over budget.
std::int64_t last_interval_ns();
// The GPU sampler's result for one frame.
void note_gpu_sample(std::uint64_t flip, double span_ms, double timed_ms, std::size_t entries,
                     const std::vector<std::pair<std::string, double>>& top);
// Asks for a capture (F11: "F11"; the request file: "bbtest snap").
void request(const char* trigger, const std::string& note);
// The confirmation line to show, or empty (the overlay, while it is due).
std::string banner();
// Post at render's coverage, published by the renderer when asked (it
// keeps its counters under its own lock).
bool post_wanted();
void publish_post(const std::string& text);

// A shader translation or pipeline compile (any thread). `kind`: "translate",
// "graphics", "compute"; `worker`: on a precompile worker (else the command
// processor's own thread); `waited`: a frame waited on it (built inline on the
// command processor, or the command processor waited for a worker's build).
void note_compile(const char* kind, const std::string& name, std::uint64_t us, bool worker, bool waited);
// Whether a frame waited on a shader or pipeline compile in the last `within_ns` (the
// "Building shaders" note, host/window.cpp).
bool compiling_recently(std::uint64_t within_ns);
// Marks the calling thread as a precompile worker (note_compile's default).
void set_worker_thread();
bool on_worker_thread();
// Totals since start: compiles by kind and where (the first-frame log line).
struct CompileTotals {
    std::uint64_t translate_worker = 0, translate_cp = 0, graphics_worker = 0, graphics_cp = 0, compute_worker = 0, compute_cp = 0;
    std::uint64_t waited = 0, waited_us = 0;
};
CompileTotals compile_totals();

}  // namespace perf
