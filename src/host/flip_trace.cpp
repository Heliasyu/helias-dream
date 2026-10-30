// HELIAS_FLIP_TRACE=1: the per-flip trace of host/flip_trace.h (debug only,
// off unless set; the profiling kit's M4 step).

#include "host/flip_trace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "hle/modules.h"
#include "host/perf_capture.h"
#include "log.h"

namespace fliptrace {
namespace {

const bool g_on = [] {
    const char* e = std::getenv("HELIAS_FLIP_TRACE");
    return e && e[0] == '1';
}();
const std::int64_t g_slow_ns = [] {
    const char* e = std::getenv("HELIAS_FLIP_TRACE_MS");
    const double ms = e ? std::atof(e) : 11.0;
    return static_cast<std::int64_t>((ms > 0 ? ms : 11.0) * 1e6);
}();

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The threads whose run / run-queue time is read at each present: the game's
// main loop (the process's first thread), its render thread and the host's
// chain from the command processor to the display.
constexpr int kHot = 7;
const char* const kHotName[kHot] = {"main", "GXRenderThread", "bb-cp0", "bb-record", "bb-submit", "bb-present", "bb-vblank"};

struct Rec {
    std::uint64_t id = 0;
    std::int64_t sub0 = 0, sub1 = 0, pick = 0, cp_end = 0, queued = 0, present = 0, interval = 0;
    std::int64_t cp_busy = 0, cp_idle = 0, main_t = 0;
    std::uint32_t backlog = 0, jobs = 0, main_us = 0;
    std::int32_t run_us[kHot] = {}, wait_us[kHot] = {};
    bool threads = false;
};
constexpr std::size_t kRecs = 8192;  // ~68 s at 120 flips a second
std::mutex g_mu;
std::array<Rec, kRecs>* g_recs = nullptr;  // under g_mu, made on the first flip
std::atomic<std::uint64_t> g_next{0};
std::atomic<std::uint32_t> g_main_us{0};
std::atomic<std::int64_t> g_main_t{0};

// The command processor's own (one thread: the graphics queue's).
thread_local std::int64_t t_pick = 0, t_last_end = 0, t_busy = 0, t_idle = 0;
thread_local std::uint32_t t_jobs = 0;

Rec* slot_locked(std::uint64_t id) {
    if (!g_recs) g_recs = new std::array<Rec, kRecs>();
    Rec& r = (*g_recs)[id % kRecs];
    return r.id == id ? &r : nullptr;
}

// ---- the hot threads' schedstat (Linux), read on the presenter's thread ----
struct Hot {
    int tid = 0, fd = -1;
    std::uint64_t run = 0, wait = 0;
    bool have = false;
};
std::array<Hot, kHot> g_hot;  // the presenter's own
std::int64_t g_hot_scan = 0;

#if defined(__linux__)
void hot_scan() {
    for (Hot& h : g_hot) {
        if (h.fd >= 0) ::close(h.fd);
        h = Hot{};
    }
    const int pid = static_cast<int>(::getpid());
    DIR* d = ::opendir("/proc/self/task");
    if (!d) return;
    while (const dirent* e = ::readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        const int tid = std::atoi(e->d_name);
        char path[64], comm[32] = {};
        std::snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
        const int cf = ::open(path, O_RDONLY | O_CLOEXEC);
        if (cf < 0) continue;
        const ssize_t n = ::read(cf, comm, sizeof(comm) - 1);
        ::close(cf);
        if (n <= 0) continue;
        comm[n] = 0;
        if (char* nl = std::strchr(comm, '\n')) *nl = 0;
        for (int k = 0; k < kHot; ++k) {
            const bool match = k == 0 ? tid == pid : std::strcmp(comm, kHotName[k]) == 0;
            if (!match || g_hot[k].tid) continue;
            std::snprintf(path, sizeof(path), "/proc/self/task/%d/schedstat", tid);
            g_hot[k].tid = tid;
            g_hot[k].fd = ::open(path, O_RDONLY | O_CLOEXEC);
        }
    }
    ::closedir(d);
}

// Run and run-queue wait (ns) of each hot thread since the last call; false
// for a thread not found (yet).
void hot_read(std::int32_t run_us[kHot], std::int32_t wait_us[kHot], bool& any) {
    const std::int64_t t = now_ns();
    bool missing = false;
    for (int k = 0; k < kHot; ++k) missing |= g_hot[k].fd < 0;
    if (g_hot_scan == 0 || (missing && t - g_hot_scan > 2'000'000'000)) {
        hot_scan();
        g_hot_scan = t;
    }
    any = false;
    for (int k = 0; k < kHot; ++k) {
        Hot& h = g_hot[k];
        run_us[k] = wait_us[k] = -1;
        if (h.fd < 0) continue;
        char buf[96];
        const ssize_t n = ::pread(h.fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {  // the thread is gone: found again at the next scan
            ::close(h.fd);
            h = Hot{};
            continue;
        }
        buf[n] = 0;
        unsigned long long run = 0, wait = 0;
        if (std::sscanf(buf, "%llu %llu", &run, &wait) != 2) continue;
        if (h.have) {
            run_us[k] = static_cast<std::int32_t>(std::min<unsigned long long>((run - h.run) / 1000, 2'000'000'000ull));
            wait_us[k] = static_cast<std::int32_t>(std::min<unsigned long long>((wait - h.wait) / 1000, 2'000'000'000ull));
            any = true;
        }
        h.run = run;
        h.wait = wait;
        h.have = true;
    }
}
#else
void hot_read(std::int32_t run_us[kHot], std::int32_t wait_us[kHot], bool& any) {
    for (int k = 0; k < kHot; ++k) run_us[k] = wait_us[k] = -1;
    any = false;
}
#endif

double ms(std::int64_t ns) { return static_cast<double>(ns) / 1e6; }
double span(std::int64_t a, std::int64_t b) { return a && b ? ms(b - a) : -1.0; }

// One flip's breakdown; `prev` the flip before it (or null).
std::string describe(const Rec& r, const Rec* prev) {
    char b[512];
    std::snprintf(b, sizeof(b),
                  "flip %llu: submit gap %.2f ms, main frame %.2f ms (ended %.2f ms before the submit), back-pressure %.2f ms (backlog %u), "
                  "submit->CP pickup %.2f ms, CP walk %.2f ms (%u jobs since the flip before: CP busy %.2f, idle %.2f ms), to video %.2f ms, "
                  "video->present %.2f ms",
                  static_cast<unsigned long long>(r.id), prev ? span(prev->sub0, r.sub0) : -1.0, r.main_us / 1000.0,
                  r.main_t ? ms(r.sub0 - r.main_t) : -1.0, span(r.sub0, r.sub1), r.backlog, span(r.sub0, r.pick), span(r.pick, r.cp_end), r.jobs,
                  ms(r.cp_busy), ms(r.cp_idle), span(r.cp_end, r.queued), span(r.queued, r.present));
    return b;
}

std::string section() {
    const std::int64_t t_end = now_ns(), t_from = t_end - 30'000'000'000;
    std::string out;
    char b[768];
    std::snprintf(b, sizeof(b),
                  "# HELIAS_FLIP_TRACE=1 (slow frame >= %.1f ms logged as `fliptrace: slow frame`). One line per presented flip of the last 30 s;\n"
                  "# t_ms = present time from base_steady_ns (this section's time - 30 s; CLOCK_MONOTONIC, as perf -k CLOCK_MONOTONIC), so\n"
                  "# align with [frames] by interval. -1 = unknown.\n"
                  "# base_steady_ns=%lld\n"
                  "# id,t_ms,interval_ms,submit_gap_ms,main_frame_ms,main_end_to_submit_ms,backpressure_ms,backlog,queue_ms,cp_walk_ms,"
                  "to_video_ms,to_present_ms,jobs,cp_busy_ms,cp_idle_ms",
                  ms(g_slow_ns), static_cast<long long>(t_from));
    out += b;
    for (int k = 0; k < kHot; ++k) {
        std::snprintf(b, sizeof(b), ",%s_run_ms,%s_wait_ms", kHotName[k], kHotName[k]);
        out += b;
    }
    out += "\n";
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_recs) return out + "(no flip traced)";
    const std::uint64_t last = g_next.load();
    const std::uint64_t first = last > kRecs ? last - kRecs + 1 : 1;
    const Rec* prev = nullptr;
    for (std::uint64_t id = first; id <= last; ++id) {
        const Rec& r = (*g_recs)[id % kRecs];
        if (r.id != id) {
            prev = nullptr;
            continue;
        }
        if (r.present >= t_from) {
            std::snprintf(b, sizeof(b), "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%.3f,%.3f,%.3f,%.3f,%u,%.3f,%.3f", static_cast<unsigned long long>(r.id),
                          ms(r.present - t_from), r.interval ? ms(r.interval) : -1.0, prev ? span(prev->sub0, r.sub0) : -1.0, r.main_us / 1000.0,
                          r.main_t ? ms(r.sub0 - r.main_t) : -1.0, span(r.sub0, r.sub1), r.backlog, span(r.sub0, r.pick), span(r.pick, r.cp_end),
                          span(r.cp_end, r.queued), span(r.queued, r.present), r.jobs, ms(r.cp_busy), ms(r.cp_idle));
            out += b;
            for (int k = 0; k < kHot; ++k) {
                std::snprintf(b, sizeof(b), ",%.3f,%.3f", r.threads && r.run_us[k] >= 0 ? r.run_us[k] / 1000.0 : -1.0,
                              r.threads && r.wait_us[k] >= 0 ? r.wait_us[k] / 1000.0 : -1.0);
                out += b;
            }
            out += "\n";
        }
        prev = &r;
    }
    return out;
}

struct Register {
    Register() {
        if (g_on) perf::add_section("flip_trace", section);
    }
} g_register;

std::int64_t g_last_present = 0;  // the presenter's own
std::uint32_t g_slow_logs = 0;

}  // namespace

bool on() { return g_on; }

std::uint64_t submit_begin() {
    if (!g_on) return 0;
    Rec r;
    r.sub0 = now_ns();
    r.id = g_next.fetch_add(1) + 1;
    r.backlog = static_cast<std::uint32_t>(hle_gnm_cp_backlog());
    r.main_us = g_main_us.load(std::memory_order_relaxed);
    r.main_t = g_main_t.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_recs) g_recs = new std::array<Rec, kRecs>();
    (*g_recs)[r.id % kRecs] = r;
    return r.id;
}

void submit_end(std::uint64_t id) {
    if (!g_on || !id) return;
    const std::int64_t t = now_ns();
    std::lock_guard<std::mutex> lk(g_mu);
    if (Rec* r = slot_locked(id)) r->sub1 = t;
}

void cp_job_begin() {
    if (!g_on) return;
    const std::int64_t t = now_ns();
    if (t_last_end) t_idle += t - t_last_end;
    t_pick = t;
}

void cp_job_end() {
    if (!g_on) return;
    const std::int64_t t = now_ns();
    if (t_pick) t_busy += t - t_pick;
    t_last_end = t;
    ++t_jobs;
}

void cp_flip(std::uint64_t id, bool queued) {
    if (!g_on || !id) return;
    const std::int64_t t = now_ns();
    std::lock_guard<std::mutex> lk(g_mu);
    Rec* r = slot_locked(id);
    if (!r) return;
    if (queued) {
        r->queued = t;
        return;
    }
    r->pick = t_pick;
    r->cp_end = t;
    r->jobs = t_jobs;
    r->cp_busy = t_busy;
    r->cp_idle = t_idle;
    t_jobs = 0;
    t_busy = t_idle = 0;
}

void main_frame(std::uint64_t busy_us) {
    if (!g_on) return;
    g_main_us.store(static_cast<std::uint32_t>(std::min<std::uint64_t>(busy_us, 0xffffffffu)), std::memory_order_relaxed);
    g_main_t.store(now_ns(), std::memory_order_relaxed);
}

void present(std::int64_t now, std::int64_t arrived_ns) {
    if (!g_on) return;
    std::int32_t run_us[kHot], wait_us[kHot];
    bool any = false;
    hot_read(run_us, wait_us, any);
    const std::int64_t interval = g_last_present ? now - g_last_present : 0;
    g_last_present = now;
    // host_log lines are at most ~1000 characters: one line per part.
    std::string lines[3];
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_recs || !arrived_ns) return;
        // The flip this present showed: the newest whose CP end is not after
        // its arrival (video queues it between cp_end and queued).
        const std::uint64_t last = g_next.load();
        Rec* r = nullptr;
        for (std::uint64_t id = last; id > 0 && id + 16 > last; --id) {
            Rec& c = (*g_recs)[id % kRecs];
            if (c.id == id && c.cp_end && c.cp_end <= arrived_ns) {
                r = &c;
                break;
            }
        }
        if (!r) return;
        r->present = now;
        r->interval = interval;
        r->threads = any;
        std::memcpy(r->run_us, run_us, sizeof(run_us));
        std::memcpy(r->wait_us, wait_us, sizeof(wait_us));
        if (interval >= g_slow_ns && interval < 250'000'000 && g_slow_logs < 5000) {
            ++g_slow_logs;
            const Rec& p = (*g_recs)[(r->id - 1) % kRecs];
            const bool have_p = r->id > 1 && p.id == r->id - 1;
            const Rec* pp = r->id > 2 ? &(*g_recs)[(r->id - 2) % kRecs] : nullptr;
            if (pp && pp->id != r->id - 2) pp = nullptr;
            char head[128];
            std::snprintf(head, sizeof(head), "fliptrace: slow frame %.2f ms (>= %.1f) at %.6f s monotonic: ", ms(interval), ms(g_slow_ns),
                          static_cast<double>(now) / 1e9);
            lines[0] = head + describe(*r, have_p ? &p : nullptr);
            if (have_p) lines[1] = "fliptrace:   the flip before: " + describe(p, pp);
            if (any) {
                lines[2] = "fliptrace:   threads run/run-queue ms since the present before:";
                for (int k = 0; k < kHot; ++k) {
                    if (run_us[k] < 0) continue;
                    char b[64];
                    std::snprintf(b, sizeof(b), " %s %.2f/%.2f", kHotName[k], run_us[k] / 1000.0, wait_us[k] / 1000.0);
                    lines[2] += b;
                }
            }
        }
    }
    for (const std::string& l : lines)
        if (!l.empty()) host_log("%s", l.c_str());
}

}  // namespace fliptrace
