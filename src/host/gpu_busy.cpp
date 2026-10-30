// The GPU busy meter: how much of the wall clock our work kept the GPU busy,
// read off the GPU's own clock, for two timestamps a command buffer.
//
// Why it is ours to measure. Windows' Task Manager takes a process's GPU %
// from the per-process engine counters (`\GPU Engine(pid_*)\Running Time`),
// which the kernel fills from what the display driver reports. On the Radeon
// 8060S with AMD's driver 32.0.31021.1015 and hardware-accelerated GPU
// scheduling on (dxdiag: "Hardware Scheduling: ... Enabled:True") the 3D
// engine's time is not reported for anyone: read 2026-10-08 after a day's
// uptime, dwm.exe - which composes the desktop with the 3D engine - had 0 of
// it, and no process more than 0.4 ms, while the copy and video engines were
// counted (dwm's copy 0.7 s, Firefox's video decode 137 s). heliasdream's perf
// passes read 0 on every engine with its GPU memory counted (2.5 GiB
// dedicated). So Task Manager cannot show this machine's 3D work, ours or
// anyone's. The lines below are the number to read instead.
//
// What it counts. A TOP_OF_PIPE timestamp as each command buffer starts and a
// BOTTOM_OF_PIPE one after its last command - the game's (begin_recording_locked
// / submit_locked) and the presenter's blit (window.cpp) - read once its fence
// has signalled. The spans overlap: the command processor starts the next
// command buffer while the last one's draws still run, and the presenter's
// blit is queued behind the game's work on the same queue. So busy time is
// the union of the spans on the GPU's timeline, not their sum: a span counts
// only past the latest end counted before it. A span includes the bubbles
// inside its command buffer (barriers draining, a pass waiting on the last);
// the GPU is ours there too, as Windows would count it. Not counted: the
// buffer shadow's upload command buffer that goes in the same submission
// ahead of the game's (off on integrated GPUs) and the start-up one-shots.
// The presenter's blit waits for the swapchain image's semaphore; AMD's,
// NVIDIA's and Mesa's drivers wait for a submission's semaphores before its
// command buffers start, so its span does not hold the display's wait - and
// were one to, the presenter's own figure in the 300-flip line would show it
// (near 1000 ms/s at V-Sync).
//
// Order. Spans arrive late and from two places: a game slot is read when the
// command processor reuses it, up to 16 submissions after it went in; the
// presenter's when it waits for its fence before recording the next. A span
// can be counted only once nothing earlier can still arrive, or a late one
// would find its time already passed over. Each source's spans come in
// submission order, which on one queue is start order, so a source with work
// out can only add spans starting at or after the last start it delivered;
// pending spans up to the least of those (over the sources with work out)
// are final. A source with nothing out holds nothing back (headless, no
// presenter). Past kMaxPending held spans - a presenter stuck for a second -
// the oldest are counted anyway (`merged early` in the report).
//
// Timestamps are timestampValidBits wide and wrap at that width (64 on AMD
// and NVIDIA; 36 on some Intel parts): values are unwrapped against the
// latest one seen, so a span may straddle a wrap. The pair is reset in the
// command buffer itself, a two-query vkCmdResetQueryPool before its first
// timestamp (the profiler resets 8,192 there): no device feature to enable,
// and nothing on the host between the read and the next use.
//
// Read in: the `frames:` line (HELIAS_FRAME_STATS: `; gpu busy N ms/s (P%)`),
// the FPS counter (host setting fps_counter: `GPU P%`), and a `gpu busy:`
// line in the 300-flip report. HELIAS_GPU_BUSY=0 turns all of it off.
#include "host/gpu.h"
#include "host/gpu_internal.h"
#include "host/perf_capture.h"
#include "core/portable.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace gpu {

namespace {

constexpr int kGame = 0, kPresenter = 1, kSources = 2;
// Spans held for order before the oldest are counted regardless: about a
// second of command buffers at 60 frames a second.
constexpr std::size_t kMaxPending = 1024;

struct Span {
    std::uint64_t t0 = 0, t1 = 0;  // unwrapped ticks
    int source = kGame;
    bool starved = false;   // test7: the GPU had run dry when it was submitted (game)
    void* site = nullptr;   // what ended its recording (game; submit_record)
    bool occl = false;      // rc3: it ended with an occlusion resolve (game)
};

// test7: the gaps between spans (the GPU idle with nothing of ours on it), by
// what came after them - a game command buffer submitted once the GPU had run
// dry ("late"), one that was already queued ("queued": the driver's and the
// kernel's time between two command buffers, or a semaphore), the presenter's
// blit - and by size; the late ones also by what ended the command buffer
// before (submit_locked's caller, or flush_locked's: the CPU waited for the
// GPU, which then had nothing until the command processor caught up).
constexpr int kGapKinds = 3, kGapLate = 0, kGapQueued = 1, kGapPresent = 2, kGapSizes = 3;
const char* const kGapKindName[kGapKinds] = {"late", "queued", "before present"};

struct Meter {
    std::mutex mu;
    bool on = false;
    double tick_ns = 1.0;
    std::uint64_t mask = ~0ull;  // timestampValidBits
    // Unwrapping: the latest raw value seen and what it unwrapped to.
    bool have_ref = false;
    std::uint64_t ref_raw = 0, ref_ext = 0;
    // Ordering: command buffers submitted and not yet read, by source, and
    // the latest start each source delivered.
    int out[kSources] = {};
    bool delivered[kSources] = {};
    std::uint64_t last_start[kSources] = {};
    std::vector<Span> pending;  // by start
    // The union so far: where it ends, and its length; by source, the same.
    std::uint64_t union_end = 0, busy = 0;
    std::uint64_t src_end[kSources] = {}, src_busy[kSources] = {}, spans[kSources] = {};
    std::uint64_t unreadable = 0, early = 0;
    std::chrono::steady_clock::time_point started{};  // when it was turned on
    // test7: the gaps (ticks, count) by kind and size (<25 us, 25-250 us, more).
    std::uint64_t gap_ticks[kGapKinds][kGapSizes] = {}, gap_n[kGapKinds][kGapSizes] = {};
    void* end_site = nullptr;                                           // the site of the span that reached union_end
    bool end_occl = false;                                              // rc3: that span ended with an occlusion resolve
    bool end_present = false;                                           // rc3: that span was the presenter's blit
    std::uint64_t q_occl_ticks = 0, q_occl_n = 0;                       // rc3: queued gaps after such a span (since the report)
    std::map<void*, std::pair<std::uint64_t, std::uint64_t>> late_by_site;  // ticks, count
    // rc4: the queued gaps by what ended the command buffer before them (as
    // late_by_site), and by how long that span was - a job shorter than the
    // kernel's round trip to queue the next leaves the GPU idle with it
    // queued (amdgpu runs two jobs ahead: sched_hw_submission).
    std::uint64_t end_len = 0;  // ticks of the span that reached union_end
    std::map<void*, std::pair<std::uint64_t, std::uint64_t>> queued_by_site;
    std::uint64_t q_len_ticks[4] = {}, q_len_n[4] = {};  // since the report
};
Meter g_meter;
// rc3: the GPU idle with work queued, totals since start (snapshots: [gpu_idle]).
std::atomic<std::uint64_t> g_idle_queued_ns{0}, g_idle_queued_n{0}, g_idle_queued_occl_ns{0}, g_idle_queued_occl_n{0},
    g_idle_queued_present_ns{0}, g_idle_queued_present_n{0};
// rc4: the same by the length of the span before the gap (<50 us, 50-200 us, 200 us-1 ms, more).
constexpr int kLenBuckets = 4;
const char* const kLenName[kLenBuckets] = {"lt50us", "50_200us", "200us_1ms", "gt1ms"};
std::atomic<std::uint64_t> g_idle_queued_len_ns[kLenBuckets], g_idle_queued_len_n[kLenBuckets];
int len_bucket(double us) { return us < 50.0 ? 0 : us < 200.0 ? 1 : us < 1000.0 ? 2 : 3; }
std::string idle_section() {
    char b[1024];
    const auto ms = [](const std::atomic<std::uint64_t>& v) { return static_cast<double>(v.load()) / 1e6; };
    std::snprintf(b, sizeof(b),
                  "idle_with_queued: ms=%.1f gaps=%llu after_occlusion_resolve_ms=%.1f gaps=%llu after_present_ms=%.1f gaps=%llu "
                  "(totals since start: the GPU idle between two of our command buffers when the second was already submitted; "
                  "the 300-flip `gpu idle:` lines in [log] have each window's, `queued` and `queued after an occlusion resolve`)",
                  ms(g_idle_queued_ns), static_cast<unsigned long long>(g_idle_queued_n.load()), ms(g_idle_queued_occl_ns),
                  static_cast<unsigned long long>(g_idle_queued_occl_n.load()), ms(g_idle_queued_present_ns),
                  static_cast<unsigned long long>(g_idle_queued_present_n.load()));
    std::string out = b;
    out += "\nidle_by_span_before:";
    for (int k = 0; k < kLenBuckets; ++k) {
        std::snprintf(b, sizeof(b), " %s_ms=%.1f %s_gaps=%llu", kLenName[k], ms(g_idle_queued_len_ns[k]), kLenName[k],
                      static_cast<unsigned long long>(g_idle_queued_len_n[k].load()));
        out += b;
    }
    std::snprintf(b, sizeof(b),
                  " reset_first_cmdbufs=%llu (rc4: queued gaps by the length of the command buffer before them; resets before "
                  "the first timestamp, HELIAS_RESET_ORDER=0 turns that off; the 300-flip `gpu idle queued after:` lines name the sites)",
                  static_cast<unsigned long long>(host_gpu_reset_first_cmdbufs()));
    out += b;
    return out;
}
const bool g_idle_section = (perf::add_section("gpu_idle", idle_section), true);

// The presenter's pair: it has one command buffer, and waits for its fence
// before recording it again.
VkQueryPool g_present_pool = VK_NULL_HANDLE;
std::atomic<bool> g_present_written{false};

std::uint64_t unwrap_locked(Meter& m, std::uint64_t raw) {
    raw &= m.mask;
    if (m.mask == ~0ull) return raw;
    if (!m.have_ref) {
        // One wrap of headroom below the first value, for a span read later
        // that started before it.
        m.have_ref = true;
        m.ref_raw = raw;
        m.ref_ext = raw + m.mask + 1;
        return m.ref_ext;
    }
    const std::uint64_t ahead = (raw - m.ref_raw) & m.mask;
    if (ahead <= (m.mask >> 1)) {
        m.ref_raw = raw;
        m.ref_ext += ahead;
        return m.ref_ext;
    }
    return m.ref_ext - ((m.ref_raw - raw) & m.mask);
}

void count_locked(Meter& m, const Span& s) {
    if (m.union_end && s.t0 > m.union_end) {
        const std::uint64_t gap = s.t0 - m.union_end;
        const int kind = s.source == kPresenter ? kGapPresent : s.starved ? kGapLate : kGapQueued;
        const double us = static_cast<double>(gap) * m.tick_ns / 1000.0;
        const int size = us < 25.0 ? 0 : us < 250.0 ? 1 : 2;
        m.gap_ticks[kind][size] += gap;
        ++m.gap_n[kind][size];
        if (kind == kGapLate) {
            auto& e = m.late_by_site[m.end_site];
            e.first += gap;
            ++e.second;
        }
        if (kind == kGapQueued) {  // rc3: the GPU idle with work queued (snapshots: [gpu_idle])
            const std::uint64_t ns = static_cast<std::uint64_t>(static_cast<double>(gap) * m.tick_ns);
            g_idle_queued_ns.fetch_add(ns, std::memory_order_relaxed);
            g_idle_queued_n.fetch_add(1, std::memory_order_relaxed);
            const int lb = len_bucket(static_cast<double>(m.end_len) * m.tick_ns / 1000.0);
            g_idle_queued_len_ns[lb].fetch_add(ns, std::memory_order_relaxed);
            g_idle_queued_len_n[lb].fetch_add(1, std::memory_order_relaxed);
            m.q_len_ticks[lb] += gap;
            ++m.q_len_n[lb];
            if (m.queued_by_site.size() < 256 || m.queued_by_site.count(m.end_site)) {
                auto& e = m.queued_by_site[m.end_present ? reinterpret_cast<void*>(1) : m.end_site];
                e.first += gap;
                ++e.second;
            }
            if (m.end_occl) {
                m.q_occl_ticks += gap;
                ++m.q_occl_n;
                g_idle_queued_occl_ns.fetch_add(ns, std::memory_order_relaxed);
                g_idle_queued_occl_n.fetch_add(1, std::memory_order_relaxed);
            } else if (m.end_present) {
                g_idle_queued_present_ns.fetch_add(ns, std::memory_order_relaxed);
                g_idle_queued_present_n.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (s.t1 > m.union_end) {
        m.end_occl = s.source == kGame && s.occl;
        m.end_present = s.source == kPresenter;
        m.end_site = s.source == kGame ? s.site : nullptr;
        m.end_len = s.t1 - s.t0;
        m.busy += s.t1 - std::max(s.t0, m.union_end);
        m.union_end = s.t1;
    }
    std::uint64_t& end = m.src_end[s.source];
    if (s.t1 > end) {
        m.src_busy[s.source] += s.t1 - std::max(s.t0, end);
        end = s.t1;
    }
    ++m.spans[s.source];
}

// Counts the pending spans no later delivery can precede.
void drain_locked(Meter& m) {
    std::uint64_t final_up_to = ~0ull;
    for (int s = 0; s < kSources; ++s) {
        if (m.out[s] > 0) final_up_to = std::min(final_up_to, m.delivered[s] ? m.last_start[s] : 0);
    }
    std::size_t i = 0;
    while (i < m.pending.size() && (m.pending[i].t0 <= final_up_to || m.pending.size() - i > kMaxPending)) {
        if (m.pending[i].t0 > final_up_to) ++m.early;
        count_locked(m, m.pending[i++]);
    }
    if (i) m.pending.erase(m.pending.begin(), m.pending.begin() + static_cast<std::ptrdiff_t>(i));
}

// A command buffer of `source` is done: its two raw timestamps, or `ok`
// false when they could not be read (a lost device).
void deliver(int source, std::uint64_t raw0, std::uint64_t raw1, bool ok, bool starved = false, void* site = nullptr, bool occl = false) {
    Meter& m = g_meter;
    std::lock_guard<std::mutex> lk(m.mu);
    if (m.out[source] > 0) --m.out[source];
    if (ok) {
        Span s;
        s.source = source;
        s.starved = starved;
        s.site = site;
        s.occl = occl;
        s.t0 = unwrap_locked(m, raw0);
        s.t1 = s.t0 + ((raw1 - raw0) & m.mask);
        // Ten seconds in one command buffer would have been a device loss:
        // a pair that was never written.
        if (static_cast<double>(s.t1 - s.t0) * m.tick_ns > 10e9) {
            ++m.unreadable;
        } else {
            auto it = m.pending.end();
            while (it != m.pending.begin() && (it - 1)->t0 > s.t0) --it;
            m.pending.insert(it, s);
            m.delivered[source] = true;
            m.last_start[source] = std::max(m.last_start[source], s.t0);
        }
    } else {
        ++m.unreadable;
    }
    drain_locked(m);
}

VkQueryPool make_pair_pool() {
    VkQueryPoolCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qci.queryCount = 2;
    VkQueryPool pool = VK_NULL_HANDLE;
    if (vkCreateQueryPool(g.device, &qci, nullptr, &pool) != VK_SUCCESS) return VK_NULL_HANDLE;
    return pool;  // reset by each command buffer that writes it, before its first timestamp
}

// Two timestamps of a pool, if the GPU wrote both: the fence that covers
// them has signalled, so this does not wait (VK_NOT_READY otherwise).
bool read_pair(VkQueryPool pool, std::uint64_t (&ts)[2]) {
    return vkGetQueryPoolResults(g.device, pool, 0, 2, sizeof(ts), ts, sizeof(ts[0]), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS;
}

}  // namespace

void busy_init_locked() {
    g.busy = false;
    if (const char* e = std::getenv("HELIAS_GPU_BUSY"); e && e[0] == '0') {
        host_log("gpu: busy meter off (HELIAS_GPU_BUSY=0)");
        return;
    }
    std::uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qs(nq);
    if (nq) vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &nq, qs.data());
    const std::uint32_t bits = g.family < nq ? qs[g.family].timestampValidBits : 0;
    if (bits == 0 || !(g.timestamp_period_ns > 0.0f)) {
        host_log("gpu: busy meter off: the queue family writes no timestamps");
        return;
    }
    bool made = true;
    for (int k = 0; k < g.slot_count && made; ++k) made = (g.slots[k].busy_pool = make_pair_pool()) != VK_NULL_HANDLE;
    if (made) made = (g_present_pool = make_pair_pool()) != VK_NULL_HANDLE;
    if (!made) {
        for (int k = 0; k < g.slot_count; ++k) {
            if (g.slots[k].busy_pool) vkDestroyQueryPool(g.device, g.slots[k].busy_pool, nullptr);
            g.slots[k].busy_pool = VK_NULL_HANDLE;
        }
        host_log("gpu: busy meter off: no timestamp query pool");
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_meter.mu);
        g_meter.mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
        g_meter.tick_ns = g.timestamp_period_ns;
        g_meter.started = std::chrono::steady_clock::now();
        g_meter.on = true;
    }
    g.busy = true;
    host_log("gpu: busy meter on: a timestamp at each command buffer's start and end, the game's and the presenter's "
             "(%u-bit, %.2f ns a tick); `gpu busy` in the frame statistics, the FPS counter and the 300-flip report; "
             "HELIAS_GPU_BUSY=0 turns it off",
             bits, static_cast<double>(g.timestamp_period_ns));
}

// With the command stream (recorder.cpp) these are ops the recorder replays
// right after its vkBeginCommandBuffer and before its vkEndCommandBuffer.
// Without it the callers have just taken g_cmd() (vkBeginCommandBuffer, the
// barrier before vkEndCommandBuffer), so the recorder holds nothing of this
// command buffer: g.cmd_ is it, and these two do not show up as recording
// sites in HELIAS_GPU_PROFILE=2's gaps.
void busy_begin_locked(Gpu::Slot& sl) {
    // A slot is retired before it is recorded again, which reads its pair; one
    // that was not would hold the game's spans back for good (`out`), so it
    // goes as unreadable.
    if (sl.busy_written) deliver(kGame, 0, 0, false);
    sl.busy_written = false;
    if (!g.busy || !sl.busy_pool) return;
    // Outside any render pass: nothing is recorded yet.
    if (stream_on()) {
        rec().reset_query_pool(sl.busy_pool, 0, 2);
        rec().write_timestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, sl.busy_pool, 0);
        return;
    }
    vkCmdResetQueryPool(g.cmd_, sl.busy_pool, 0, 2);
    vkCmdWriteTimestamp(g.cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, sl.busy_pool, 0);
}

void busy_end_locked(Gpu::Slot& sl) {
    if (!g.busy || !sl.busy_pool) return;
    if (stream_on()) {
        rec().write_timestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, sl.busy_pool, 1);
    } else {
        vkCmdWriteTimestamp(g.cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, sl.busy_pool, 1);
    }
    sl.busy_written = true;
}

void busy_submitted_locked(Gpu::Slot& sl) {
    if (!sl.busy_written) return;
    std::lock_guard<std::mutex> lk(g_meter.mu);
    ++g_meter.out[kGame];
}

std::string busy_exit_report() {
    const GpuBusy b = host_gpu_busy();
    if (!b.on) return {};
    double secs = 0.0;
    {
        std::lock_guard<std::mutex> lk(g_meter.mu);
        secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_meter.started).count();
    }
    if (secs <= 0.0) return {};
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "gpu: busy over the run: %.1f%% of %.0f s (%.0f ms/s; the game's command buffers %.0f s, the presenter's %.1f s; "
                  "%llu command buffers, %llu presents)",
                  static_cast<double>(b.busy_ns) / 1e7 / secs, secs, static_cast<double>(b.busy_ns) / 1e6 / secs,
                  static_cast<double>(b.game_ns) / 1e9, static_cast<double>(b.present_ns) / 1e9,
                  static_cast<unsigned long long>(b.cmdbufs), static_cast<unsigned long long>(b.presents));
    return buf;
}

void busy_retired_locked(Gpu::Slot& sl, bool ok) {
    if (!sl.busy_written) return;
    sl.busy_written = false;
    std::uint64_t ts[2] = {0, 0};
    const bool have = ok && read_pair(sl.busy_pool, ts);
    void* site = nullptr;
    const bool starved = submit_record(sl.serial, &site);
    deliver(kGame, ts[0], ts[1], have, starved, site, submit_had_occlusion(sl.serial));
}

}  // namespace gpu

using namespace gpu;

void host_gpu_busy_present_begin(void* cmd) {
    if (!g.busy || !g_present_pool) return;
    const auto c = static_cast<VkCommandBuffer>(cmd);
    vkCmdResetQueryPool(c, g_present_pool, 0, 2);
    vkCmdWriteTimestamp(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_present_pool, 0);
}

void host_gpu_busy_present_end(void* cmd) {
    if (!g.busy || !g_present_pool) return;
    vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(cmd), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_present_pool, 1);
    g_present_written.store(true, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_meter.mu);
    ++g_meter.out[kPresenter];
}

void host_gpu_busy_present_done() {
    if (!g_present_written.exchange(false, std::memory_order_relaxed)) return;
    std::uint64_t ts[2] = {0, 0};
    const bool have = read_pair(g_present_pool, ts);
    deliver(kPresenter, ts[0], ts[1], have);
}

GpuBusy host_gpu_busy() {
    GpuBusy b;
    std::lock_guard<std::mutex> lk(g_meter.mu);
    if (!g_meter.on) return b;
    const double k = g_meter.tick_ns;
    b.on = true;
    b.busy_ns = static_cast<std::uint64_t>(static_cast<double>(g_meter.busy) * k);
    b.game_ns = static_cast<std::uint64_t>(static_cast<double>(g_meter.src_busy[kGame]) * k);
    b.present_ns = static_cast<std::uint64_t>(static_cast<double>(g_meter.src_busy[kPresenter]) * k);
    b.cmdbufs = g_meter.spans[kGame];
    b.presents = g_meter.spans[kPresenter];
    return b;
}

std::string host_gpu_busy_report() {
    // The first call (the first flip's report) only sets the window's start.
    static bool started = false;
    static GpuBusy last;
    static std::chrono::steady_clock::time_point last_t;
    const GpuBusy now = host_gpu_busy();
    const auto t = std::chrono::steady_clock::now();
    const double secs = started ? std::chrono::duration<double>(t - last_t).count() : 0.0;
    started = true;
    std::uint64_t unreadable = 0, early = 0;
    {
        std::lock_guard<std::mutex> lk(g_meter.mu);
        unreadable = g_meter.unreadable;
        early = g_meter.early;
    }
    std::string r;
    if (now.on && secs >= 0.5) {  // not the first flips' reports, a few ms apart
        const double ms = static_cast<double>(now.busy_ns - last.busy_ns) / 1e6 / secs;
        char buf[320];
        std::snprintf(buf, sizeof(buf),
                      "gpu busy: %.1f%% of %.1f s (%.0f ms/s; the game's command buffers %.0f ms/s, the presenter's %.1f ms/s; "
                      "%llu command buffers, %llu presents; unreadable %llu, merged early %llu)",
                      ms / 10.0, secs, ms, static_cast<double>(now.game_ns - last.game_ns) / 1e6 / secs,
                      static_cast<double>(now.present_ns - last.present_ns) / 1e6 / secs,
                      static_cast<unsigned long long>(now.cmdbufs - last.cmdbufs),
                      static_cast<unsigned long long>(now.presents - last.presents), static_cast<unsigned long long>(unreadable),
                      static_cast<unsigned long long>(early));
        r = buf;
    }
    last = now;
    last_t = t;
    return r;
}

// test7: the second line under `gpu busy:` - where the idle time went since
// the last report (see Meter's gaps), the submissions' hand-over latency, the
// command processor's waits for the GPU, and HELIAS_SUBMIT_SPLIT's splits.
std::string host_gpu_idle_report() {
    static bool started = false;
    static std::chrono::steady_clock::time_point last_t;
    static SubmitLatencyTotals last;
    const auto t = std::chrono::steady_clock::now();
    const double secs = started ? std::chrono::duration<double>(t - last_t).count() : 0.0;
    started = true;
    last_t = t;
    const SubmitLatencyTotals lat = submit_latency_totals();
    std::uint64_t ticks[kGapKinds][kGapSizes], n[kGapKinds][kGapSizes];
    std::vector<std::pair<void*, std::pair<std::uint64_t, std::uint64_t>>> sites;
    std::uint64_t q_occl_ticks = 0, q_occl_n = 0;
    std::vector<std::pair<void*, std::pair<std::uint64_t, std::uint64_t>>> qsites;
    std::uint64_t q_len_ticks[kLenBuckets] = {}, q_len_n[kLenBuckets] = {};
    double tick_ns = 1.0;
    {
        std::lock_guard<std::mutex> lk(g_meter.mu);
        if (!g_meter.on) return {};
        tick_ns = g_meter.tick_ns;
        std::memcpy(ticks, g_meter.gap_ticks, sizeof(ticks));
        std::memcpy(n, g_meter.gap_n, sizeof(n));
        std::memset(g_meter.gap_ticks, 0, sizeof(g_meter.gap_ticks));
        std::memset(g_meter.gap_n, 0, sizeof(g_meter.gap_n));
        sites.assign(g_meter.late_by_site.begin(), g_meter.late_by_site.end());
        g_meter.late_by_site.clear();
        q_occl_ticks = g_meter.q_occl_ticks;
        q_occl_n = g_meter.q_occl_n;
        g_meter.q_occl_ticks = g_meter.q_occl_n = 0;
        qsites.assign(g_meter.queued_by_site.begin(), g_meter.queued_by_site.end());
        g_meter.queued_by_site.clear();
        std::memcpy(q_len_ticks, g_meter.q_len_ticks, sizeof(q_len_ticks));
        std::memcpy(q_len_n, g_meter.q_len_n, sizeof(q_len_n));
        std::memset(g_meter.q_len_ticks, 0, sizeof(g_meter.q_len_ticks));
        std::memset(g_meter.q_len_n, 0, sizeof(g_meter.q_len_n));
    }
    const SubmitLatencyTotals prev = last;
    last = lat;
    if (secs < 0.5) return {};
    const double per_s = 1.0 / secs;
    auto ms_s = [&](std::uint64_t tk) { return static_cast<double>(tk) * tick_ns / 1e6 * per_s; };
    std::uint64_t all = 0, all_n = 0;
    for (int k = 0; k < kGapKinds; ++k)
        for (int z = 0; z < kGapSizes; ++z) {
            all += ticks[k][z];
            all_n += n[k][z];
        }
    std::string r;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "gpu idle: %.1f ms/s in %.0f gaps/s;", ms_s(all), static_cast<double>(all_n) * per_s);
    r += buf;
    for (int k = 0; k < kGapKinds; ++k) {
        std::uint64_t kt = 0, kn = 0;
        for (int z = 0; z < kGapSizes; ++z) {
            kt += ticks[k][z];
            kn += n[k][z];
        }
        std::snprintf(buf, sizeof(buf), " %s %.2f ms/s (%.0f/s: <25us %.2f, 25-250us %.2f, >250us %.2f ms/s);", kGapKindName[k], ms_s(kt),
                      static_cast<double>(kn) * per_s, ms_s(ticks[k][0]), ms_s(ticks[k][1]), ms_s(ticks[k][2]));
        r += buf;
    }
    // rc3: the queued gaps after a command buffer that ended with an occlusion resolve.
    std::snprintf(buf, sizeof(buf), " queued after an occlusion resolve %.2f ms/s (%.0f/s);", ms_s(q_occl_ticks), static_cast<double>(q_occl_n) * per_s);
    r += buf;
    const std::uint64_t subs = lat.n - prev.n;
    const double sn = subs ? static_cast<double>(subs) : 1.0;
    std::snprintf(buf, sizeof(buf),
                  " submits %.0f/s, %.0f%% found the GPU idle; hand-over to recorded %.0f us (max %.0f), to vkQueueSubmit %.0f us (max %.0f), "
                  "vkQueueSubmit %.0f us (max %.0f);",
                  static_cast<double>(subs) * per_s, 100.0 * static_cast<double>(lat.starved - prev.starved) / sn,
                  static_cast<double>(lat.record_ns - prev.record_ns) / 1e3 / sn, static_cast<double>(lat.record_max) / 1e3,
                  static_cast<double>(lat.queue_ns - prev.queue_ns) / 1e3 / sn, static_cast<double>(lat.queue_max) / 1e3,
                  static_cast<double>(lat.submit_ns - prev.submit_ns) / 1e3 / sn, static_cast<double>(lat.submit_max) / 1e3);
    r += buf;
    std::snprintf(buf, sizeof(buf), " cpu waits for the GPU (flush_locked) %.0f/s, %.2f ms/s; splits %.0f/s",
                  static_cast<double>(lat.flushes - prev.flushes) * per_s, static_cast<double>(lat.flush_ns - prev.flush_ns) / 1e6 * per_s,
                  static_cast<double>(lat.splits - prev.splits) * per_s);
    r += buf;
    // test7c, HELIAS_SUBMIT_BATCH: merged submissions, the jobs in them, and the holds.
    std::snprintf(buf, sizeof(buf), "; batched %.0f jobs/s into %.0f submits/s, held %.0f/s for %.2f ms/s",
                  static_cast<double>(lat.batch_jobs - prev.batch_jobs) * per_s, static_cast<double>(lat.batch_submits - prev.batch_submits) * per_s,
                  static_cast<double>(lat.batch_holds - prev.batch_holds) * per_s,
                  static_cast<double>(lat.batch_hold_ns - prev.batch_hold_ns) / 1e6 * per_s);
    r += buf;
    // The late gaps by what ended the command buffer before them, the top
    // eight, as offsets in heliasdream's image (tools/gpu_gaps.py --idle names them).
    std::sort(sites.begin(), sites.end(), [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
    if (!sites.empty()) r += "\n[heliasdream]   gpu idle late after:";
    for (std::size_t i = 0; i < sites.size() && i < 8; ++i) {
        if (sites[i].first)
            std::snprintf(buf, sizeof(buf), " %llx=%.2fms/s(%.0f/s)",
                          static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(sites[i].first) - host_image_base()),
                          ms_s(sites[i].second.first), static_cast<double>(sites[i].second.second) * per_s);
        else
            std::snprintf(buf, sizeof(buf), " present=%.2fms/s(%.0f/s)", ms_s(sites[i].second.first),
                          static_cast<double>(sites[i].second.second) * per_s);
        r += buf;
    }
    // rc4: the queued gaps the same way - by the site that ended the command
    // buffer before them (present: the blit) - and by that command buffer's length.
    std::sort(qsites.begin(), qsites.end(), [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
    if (!qsites.empty()) {
        r += "\n[heliasdream]   gpu idle queued after:";
        for (std::size_t i = 0; i < qsites.size() && i < 10; ++i) {
            if (qsites[i].first == reinterpret_cast<void*>(1))
                std::snprintf(buf, sizeof(buf), " present=%.2fms/s(%.0f/s)", ms_s(qsites[i].second.first),
                              static_cast<double>(qsites[i].second.second) * per_s);
            else if (qsites[i].first)
                std::snprintf(buf, sizeof(buf), " %llx=%.2fms/s(%.0f/s)",
                              static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(qsites[i].first) - host_image_base()),
                              ms_s(qsites[i].second.first), static_cast<double>(qsites[i].second.second) * per_s);
            else
                std::snprintf(buf, sizeof(buf), " unknown=%.2fms/s(%.0f/s)", ms_s(qsites[i].second.first),
                              static_cast<double>(qsites[i].second.second) * per_s);
            r += buf;
        }
        r += "; by the command buffer before:";
        for (int k = 0; k < kLenBuckets; ++k) {
            std::snprintf(buf, sizeof(buf), " %s %.2fms/s(%.0f/s)", kLenName[k], ms_s(q_len_ticks[k]), static_cast<double>(q_len_n[k]) * per_s);
            r += buf;
        }
    }
    return r;
}
