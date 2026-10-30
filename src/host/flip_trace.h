#pragma once

// heliasdream-vk profiling kit (after rc5b): HELIAS_FLIP_TRACE=1, a debug trace of
// every flip the game submits, for the periodic ~12 ms frame whose flip comes
// ~4 ms late from the game / command-processor side (rc5 ANALYSIS, M4). Off
// unless the variable is set: then every hook below is one load of a constant
// and a branch, and nothing is recorded or logged.
//
// Per flip, steady-clock times of:
//   the game thread entering sceGnmSubmitAndFlipCommandBuffers (sub0) and
//   returning from the command processor's back-pressure (sub1);
//   the command processor taking the flip's job (pick), having walked and
//   submitted it (cp_end), and the flip queued for the presenter (queued);
//   the present returning (present, matched by the flip's arrival time);
// plus the CP backlog at the submit, the CP's jobs, busy and idle time since
// the flip before, the main loop's last frame (engine/frame_rate.cpp) and, on
// Linux, each hot thread's run and run-queue wait time between two presents
// (/proc/self/task/<tid>/schedstat, read on bb-present).
//
// A present interval of HELIAS_FLIP_TRACE_MS (11) or more, under 250 ms (not a
// load), logs one `fliptrace: slow frame` line with this flip and the one
// before it broken down, and the threads' run/wait in that interval. Every
// F11 snapshot gets a [flip_trace] section with one CSV line per flip of the
// window (tools: bbprofile_analyze.py / fliptrace.py).
// Cost when on: ~4 uncontended mutex sections a flip on the game and CP
// threads, and 7 small preads a present on bb-present.

#include <cstdint>

namespace fliptrace {

bool on();
// The game thread, entering sceGnmSubmitAndFlipCommandBuffers: the flip's id
// (0 when off). submit_end after the job is queued and back-pressure let go.
std::uint64_t submit_begin();
void submit_end(std::uint64_t id);
// The command processor of the graphics queue: a job taken, a job walked and
// submitted (before its completion callback).
void cp_job_begin();
void cp_job_end();
// In the flip job's completion callback, on the command processor: before
// (queued=false) and after (queued=true) the flip is handed to video.
void cp_flip(std::uint64_t id, bool queued);
// The main loop's frame work (engine/frame_rate.cpp), on the main thread.
void main_frame(std::uint64_t busy_us);
// The presenter, after each present returned: `arrived_ns` the flip's arrival
// (0 unknown), steady clock.
void present(std::int64_t now_ns, std::int64_t arrived_ns);

}  // namespace fliptrace
