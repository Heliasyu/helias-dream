// heliasdream-vk rc2: the performance capture (host/perf_capture.h).
#include "host/perf_capture.h"

#include "core/portable.h"
#include "engine/world_chr.h"
#include "host/gpu.h"
#include "host/options.h"
#include "host/pass_timer.h"
#include "host/translation_cache.h"
#include "host/updater.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(__linux__)
#include <dirent.h>
#include <unistd.h>
extern char** environ;
#endif

namespace perf {
namespace {

constexpr std::int64_t kWindowNs = 30'000'000'000;  // what a capture keeps

std::int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- the log's last lines: a lock-free ring (the fault handler logs) ----
constexpr std::size_t kLogSlots = 4096, kLogText = 320;
struct LogSlot {
    std::atomic<std::uint64_t> seq{0};  // odd while written
    std::int64_t t = 0;
    std::uint32_t len = 0;
    char text[kLogText];
};
LogSlot* g_log = nullptr;
std::atomic<std::uint64_t> g_log_next{0};

void log_tap(const char* line, std::size_t len) {
    LogSlot* ring = g_log;
    if (!ring) return;
    const std::uint64_t i = g_log_next.fetch_add(1, std::memory_order_relaxed);
    LogSlot& s = ring[i % kLogSlots];
    s.seq.store(2 * i + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    s.t = steady_ns();
    const std::size_t n = std::min(len, kLogText - 1);
    std::memcpy(s.text, line, n);
    s.len = static_cast<std::uint32_t>(n);
    std::atomic_thread_fence(std::memory_order_release);
    s.seq.store(2 * i + 2, std::memory_order_release);
}

std::vector<std::pair<std::int64_t, std::string>> log_lines(std::int64_t from) {
    std::vector<std::pair<std::int64_t, std::string>> out;
    if (!g_log) return out;
    const std::uint64_t end = g_log_next.load(std::memory_order_acquire);
    const std::uint64_t begin = end > kLogSlots ? end - kLogSlots : 0;
    for (std::uint64_t i = begin; i < end; ++i) {
        LogSlot& s = g_log[i % kLogSlots];
        const std::uint64_t a = s.seq.load(std::memory_order_acquire);
        if (a != 2 * i + 2) continue;
        char buf[kLogText];
        const std::int64_t t = s.t;
        const std::uint32_t n = std::min<std::uint32_t>(s.len, kLogText - 1);
        std::memcpy(buf, s.text, n);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.seq.load(std::memory_order_relaxed) != a || t < from) continue;
        std::string line(buf, n);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        out.emplace_back(t, std::move(line));
    }
    return out;
}

// ---- every presented frame ----
struct Frame {
    std::int64_t t = 0;        // steady ns, the present returned
    std::int32_t dt_us = 0;    // since the present before
    std::int32_t lat_us = -1;  // flip queued -> present returned; -1 unknown
    std::uint32_t dropped = 0, cp_waits = 0;  // totals
    std::uint64_t cp_wait_ns = 0;              // total
    std::int32_t rec_us = -1, sub_us = -1, pace_us = -1, pres_us = -1;  // rc3: PresentSplit
    bool prio = false;
};
constexpr std::size_t kFrames = 8192;  // 30 s at 240 fps
std::mutex g_frames_mu;
std::array<Frame, kFrames>* g_frames = nullptr;
std::uint64_t g_frame_n = 0;  // under g_frames_mu
std::atomic<std::int64_t> g_last_present{0}, g_last_interval{0};

// ---- every second (the capture thread) ----
struct Second {
    std::int64_t t = 0;
    std::uint32_t block = 0;  // 0: no player
    double gpu_pct = -1.0, game_ms = 0.0, present_ms = 0.0;
    std::vector<std::pair<std::string, double>> threads;  // CPU ms in that second, by name, the busiest
};
std::deque<Second> g_seconds;  // the capture thread's own

// ---- the GPU sampler's frames ----
struct Sample {
    std::int64_t t = 0;
    std::uint64_t flip = 0;
    double span_ms = 0, timed_ms = 0;
    std::size_t entries = 0;
    std::vector<std::pair<std::string, double>> top;
};
std::mutex g_samples_mu;
std::deque<Sample> g_samples;

// ---- requests, the banner, post at render ----
std::mutex g_req_mu;
std::condition_variable g_req_cv;
bool g_req = false;
std::string g_req_note, g_req_trigger;
std::mutex g_banner_mu;
std::string g_banner;
std::int64_t g_banner_until = 0;
std::atomic<bool> g_post_want{false};
std::mutex g_post_mu;
std::string g_post_text;
std::int64_t g_post_t = 0;
std::atomic<bool> g_started{false};
std::mutex g_sections_mu;
std::vector<std::pair<std::string, SectionFn>>& sections() {
    static std::vector<std::pair<std::string, SectionFn>> v;
    return v;
}
std::int32_t split_us(std::int64_t ns) { return ns < 0 ? -1 : static_cast<std::int32_t>(std::min<std::int64_t>(ns / 1000, 2'000'000'000)); }

// ---- shader translations and pipeline compiles ----
struct Compile {
    std::int64_t t = 0;
    std::string kind, name;
    std::uint64_t us = 0;
    bool worker = false, waited = false;
};
std::mutex g_compiles_mu;
std::deque<Compile> g_compiles;  // the newest 4096
std::atomic<std::uint64_t> g_ct[6] = {};  // translate w/cp, graphics w/cp, compute w/cp
std::atomic<std::uint64_t> g_ct_waited{0}, g_ct_waited_us{0};
thread_local bool t_worker = false;

const char* area_name(std::uint32_t block) {
    switch (block >> 16) {
        case 0x1500: return "Hunter's Dream";
        case 0x1800: return "Cathedral Ward";
        case 0x1801: return "Central Yharnam";
        case 0x1700: return "Old Yharnam";
        case 0x1b00: return "Forbidden Woods";
        case 0x2000: return "Byrgenwerth";
        case 0x1a00: return "Nightmare of Mensis";
        case 0x2400: return "Fishing Hamlet";
        case 0x2300: return "Research Hall / Astral Clocktower";
        case 0x2200: return "Hunter's Nightmare";
        default: return "";
    }
}
std::string block_text(std::uint32_t b) {
    if (!b) return "none (title, menu or a load)";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "m%02u_%02u_%02u_%02u", b >> 24, (b >> 16) & 0xff, (b >> 8) & 0xff, b & 0xff);
    std::string s = buf;
    if (*area_name(b)) s += std::string(" ") + area_name(b);
    return s;
}
std::uint32_t current_block() {
    std::uint32_t b = 0;
    return world_player_block(&b) ? b : 0;
}

std::string snap_dir() {
    const char* e = std::getenv("HELIAS_SNAP_DIR");
    return e && *e ? std::string(e) : std::string("snaps");
}

void set_banner(const std::string& s, double seconds) {
    std::lock_guard<std::mutex> lk(g_banner_mu);
    g_banner = s;
    g_banner_until = steady_ns() + static_cast<std::int64_t>(seconds * 1e9);
}

#if defined(__linux__)
// Each thread's CPU time (utime + stime, ns) by tid, with its name.
std::map<int, std::pair<std::string, std::uint64_t>> read_threads() {
    std::map<int, std::pair<std::string, std::uint64_t>> out;
    static const long hz = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100;
    DIR* d = opendir("/proc/self/task");
    if (!d) return out;
    while (dirent* de = readdir(d)) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/self/task/%s/stat", de->d_name);
        FILE* f = std::fopen(path, "r");
        if (!f) continue;
        char buf[1024];
        const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        buf[n] = 0;
        char* l = std::strchr(buf, '(');
        char* r = std::strrchr(buf, ')');
        if (!l || !r || r < l) continue;
        std::string name(l + 1, static_cast<std::size_t>(r - l - 1));
        // After ") ": state(3) ... utime is field 14, stime 15.
        char* p = r + 2;
        unsigned long long ut = 0, st = 0;
        int field = 3;
        while (*p && field < 14) {
            if (*p == ' ') ++field;
            ++p;
        }
        if (std::sscanf(p, "%llu %llu", &ut, &st) != 2) continue;
        out[std::atoi(de->d_name)] = {name, (ut + st) * 1000000000ull / static_cast<unsigned long long>(hz)};
    }
    closedir(d);
    return out;
}
#endif

void tick_second(std::map<int, std::pair<std::string, std::uint64_t>>& last_threads, GpuBusy& last_busy, std::int64_t& last_t) {
    Second s;
    s.t = steady_ns();
    s.block = current_block();
    const GpuBusy b = host_gpu_busy();
    const double secs = last_t ? static_cast<double>(s.t - last_t) / 1e9 : 0.0;
    if (b.on && last_busy.on && secs > 0.2) {
        s.gpu_pct = static_cast<double>(b.busy_ns - last_busy.busy_ns) / 1e7 / secs;
        s.game_ms = static_cast<double>(b.game_ns - last_busy.game_ns) / 1e6 / secs;
        s.present_ms = static_cast<double>(b.present_ns - last_busy.present_ns) / 1e6 / secs;
    }
    last_busy = b;
#if defined(__linux__)
    auto now = read_threads();
    if (secs > 0.2) {
        std::map<std::string, double> by_name;
        for (const auto& [tid, v] : now) {
            auto it = last_threads.find(tid);
            const std::uint64_t was = it != last_threads.end() ? it->second.second : v.second;
            by_name[v.first] += static_cast<double>(v.second - std::min(was, v.second)) / 1e6 / secs;
        }
        std::vector<std::pair<std::string, double>> v(by_name.begin(), by_name.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& c) { return a.second > c.second; });
        if (v.size() > 12) v.resize(12);
        s.threads = std::move(v);
    }
    last_threads = std::move(now);
#else
    (void)last_threads;
#endif
    last_t = s.t;
    g_seconds.push_back(std::move(s));
    while (g_seconds.size() > 40) g_seconds.pop_front();
}

std::string local_time(const char* fmt) {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), fmt, &tm);
    return buf;
}

double pct(std::vector<double> v, double p) {  // the p-th percentile (0..1) of v
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t i = std::min(v.size() - 1, static_cast<std::size_t>(std::ceil(p * static_cast<double>(v.size())) - 1));
    return v[i];
}

void rgp_after_snapshot(const std::filesystem::path& snap, const std::string& note, const std::string& area);

void capture(const std::string& trigger, const std::string& note) {
    const std::int64_t t_end = steady_ns(), t_from = t_end - kWindowNs;
    // The renderer's coverage line, fresh if it draws within 200 ms.
    g_post_want.store(true);
    std::vector<Frame> frames;
    {
        std::lock_guard<std::mutex> lk(g_frames_mu);
        const std::uint64_t n = std::min<std::uint64_t>(g_frame_n, kFrames);
        frames.reserve(static_cast<std::size_t>(n));
        for (std::uint64_t i = g_frame_n - n; i < g_frame_n; ++i) {
            const Frame& f = (*g_frames)[i % kFrames];
            if (f.t >= t_from) frames.push_back(f);
        }
    }
    const auto lines = log_lines(t_from);
    std::vector<Sample> samples;
    {
        std::lock_guard<std::mutex> lk(g_samples_mu);
        for (const Sample& s : g_samples)
            if (s.t >= t_from - 15'000'000'000) samples.push_back(s);  // the last one before the window too
    }
    const std::string pass_line = host_gpu_pass_timing();
    const std::string settings = host_options_dump();
    for (int i = 0; i < 20 && g_post_want.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::string post;
    std::int64_t post_t = 0;
    {
        std::lock_guard<std::mutex> lk(g_post_mu);
        post = g_post_text;
        post_t = g_post_t;
    }
    const std::uint32_t block = current_block();

    // The summary, from the frames (what MangoHud times: present to present).
    std::vector<double> dt;
    double sum_ms = 0;
    int over85 = 0, over9 = 0, over12 = 0, over_load = 0;
    double max_ms = 0, lat_sum = 0, lat_max = 0;
    int lat_n = 0;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        const double ms = frames[i].dt_us / 1000.0;
        if (ms > 250.0) {  // a load or a pause: counted apart, not in the averages
            ++over_load;
            continue;
        }
        dt.push_back(ms);
        sum_ms += ms;
        over85 += ms > 8.5;
        over9 += ms > 9.0;
        over12 += ms > 12.0;
        max_ms = std::max(max_ms, ms);
        if (frames[i].lat_us >= 0) {
            lat_sum += frames[i].lat_us / 1000.0;
            lat_max = std::max(lat_max, frames[i].lat_us / 1000.0);
            ++lat_n;
        }
    }
    const double avg_fps = sum_ms > 0 ? 1000.0 * static_cast<double>(dt.size()) / sum_ms : 0.0;
    const double p99 = pct(dt, 0.99), p999 = pct(dt, 0.999);
    const std::uint32_t drops = frames.size() > 1 ? frames.back().dropped - frames.front().dropped : 0;
    const std::uint32_t cpw = frames.size() > 1 ? frames.back().cp_waits - frames.front().cp_waits : 0;
    const double cpw_ms = frames.size() > 1 ? static_cast<double>(frames.back().cp_wait_ns - frames.front().cp_wait_ns) / 1e6 : 0.0;
    const double secs = frames.size() > 1 ? static_cast<double>(frames.back().t - frames.front().t) / 1e9 : 0.0;

    std::ostringstream o;
    char buf[512];
    o << "# Helia's Dream performance snapshot (F11 / bbtest snap; docs/vk/TESTING.md)\n";
    o << "version: " << updater::current_version() << "\n";
    o << "time: " << local_time("%Y-%m-%d %H:%M:%S %Z") << "\n";
    o << "trigger: " << trigger << "\n";
    o << "note: " << note << "\n";
    o << "area: " << block_text(block) << "\n";
    std::snprintf(buf, sizeof(buf),
                  "summary: frames=%zu seconds=%.1f avg_fps=%.1f low1=%.1f low01=%.1f over8.5=%d over9=%d over12=%d max_ms=%.2f "
                  "loads=%d drops=%u cp_waits=%u cp_wait_ms=%.1f flip_to_present_avg_ms=%.2f flip_to_present_max_ms=%.2f\n",
                  dt.size(), secs, avg_fps, p99 > 0 ? 1000.0 / p99 : 0.0, p999 > 0 ? 1000.0 / p999 : 0.0, over85, over9, over12, max_ms,
                  over_load, drops, cpw, cpw_ms, lat_n ? lat_sum / lat_n : 0.0, lat_max);
    o << buf;
    o << "(1%/0.1% lows: the fps of the 99th/99.9th percentile frame time; intervals over 250 ms are loads, counted apart)\n";
    {  // rc3: flip to present, split
        double r = 0, sb = 0, pc = 0, pr = 0, sb_max = 0, pc_max = 0, pr_max = 0;
        int n = 0, prio = 0;
        for (std::size_t i = 1; i < frames.size(); ++i) {
            const Frame& f = frames[i];
            if (f.sub_us < 0 || f.dt_us > 250000) continue;
            ++n;
            prio += f.prio;
            r += f.rec_us / 1000.0;
            sb += f.sub_us / 1000.0;
            pc += f.pace_us / 1000.0;
            pr += f.pres_us / 1000.0;
            sb_max = std::max(sb_max, f.sub_us / 1000.0);
            pc_max = std::max(pc_max, f.pace_us / 1000.0);
            pr_max = std::max(pr_max, f.pres_us / 1000.0);
        }
        const double d = n ? n : 1;
        std::snprintf(buf, sizeof(buf),
                      "flip_to_present_split: frames=%d record_avg_ms=%.2f submit_avg_ms=%.2f submit_max_ms=%.2f pacer_avg_ms=%.2f pacer_max_ms=%.2f "
                      "present_call_avg_ms=%.2f present_call_max_ms=%.2f priority_submits=%d latency_limit_waits=%u latency_limit_wait_ms=%.1f\n",
                      n, r / d, sb / d, sb_max, pc / d, pc_max, pr / d, pr_max, prio, cpw, cpw_ms);
        o << buf;
        o << "(record: flip queued -> its blit recorded; submit: blit recorded -> on the queue (overlaps the pacer when queued); pacer: "
             "the pacer's wait; present_call: vkQueuePresentKHR)\n";
    }
    {  // rc3: the modules' counters
        std::vector<std::pair<std::string, SectionFn>> secs;
        {
            std::lock_guard<std::mutex> lk(g_sections_mu);
            secs = sections();
        }
        for (const auto& [title, fn] : secs) o << "\n[" << title << "]\n" << fn() << "\n";
    }
    // The latest GPU sample's top ten.
    o << "\n[passes]  # the newest GPU sample: GPU ms by pass (a render pass named by its first draw's program; compute by its shader; "
         "rt-copy-* the host's copies)\n";
    if (!samples.empty()) {
        const Sample& s = samples.back();
        std::snprintf(buf, sizeof(buf), "sample: flip=%llu age_s=%.1f span_ms=%.2f timed_ms=%.2f entries=%zu\n",
                      static_cast<unsigned long long>(s.flip), static_cast<double>(t_end - s.t) / 1e9, s.span_ms, s.timed_ms, s.entries);
        o << buf;
        for (std::size_t i = 0; i < s.top.size() && i < 10; ++i) {
            std::snprintf(buf, sizeof(buf), "pass %2zu %8.3f ms  %s\n", i + 1, s.top[i].second, s.top[i].first.c_str());
            o << buf;
        }
    } else {
        o << "sample: none in the last 45 s (one every ~10 s, skipped after a frame over budget; HELIAS_GPU_SAMPLE_SLOW=1: every ~3 s, only after one)\n";
    }
    o << "\n[samples]  # every GPU sample kept\n";
    for (const Sample& s : samples) {
        std::snprintf(buf, sizeof(buf), "flip=%llu age_s=%.1f span_ms=%.2f timed_ms=%.2f:", static_cast<unsigned long long>(s.flip),
                      static_cast<double>(t_end - s.t) / 1e9, s.span_ms, s.timed_ms);
        o << buf;
        for (const auto& [name, ms] : s.top) {
            std::snprintf(buf, sizeof(buf), " %s=%.3f", name.c_str(), ms);
            o << buf;
        }
        o << "\n";
    }
    o << "\n[upscaler]  # FSR and its sharpening, the host's passes (pass timer, averaged over the timed frames)\n" << pass_line << "\n";
    o << "\n[post_at_render]\n";
    if (!post.empty()) {
        std::snprintf(buf, sizeof(buf), "(published %.1f s before the capture)\n", static_cast<double>(t_end - post_t) / 1e9);
        o << buf << post << "\n";
    } else {
        o << "(no frame drew since the start)\n";
    }
    // Compile stutter told apart from GPU load: what was translated or compiled
    // in the window, and whether a frame waited on it.
    {
        std::vector<Compile> cs;
        {
            std::lock_guard<std::mutex> lk(g_compiles_mu);
            for (const Compile& c : g_compiles)
                if (c.t >= t_from) cs.push_back(c);
        }
        std::uint64_t n_wait = 0, us_wait = 0, us_all = 0;
        for (const Compile& c : cs) {
            us_all += c.us;
            if (c.waited) ++n_wait, us_wait += c.us;
        }
        std::snprintf(buf, sizeof(buf),
                      "\n[compiles]  # shader translations and pipeline compiles in the window: %zu, %.1f ms in all; %llu a frame waited on, %.1f ms\n"
                      "# t_ms (from the window's start), kind, ms, where (worker / cp = the command processor), waited, name\n",
                      cs.size(), us_all / 1000.0, static_cast<unsigned long long>(n_wait), us_wait / 1000.0);
        o << buf;
        for (const Compile& c : cs) {
            std::snprintf(buf, sizeof(buf), "%.3f, %s, %.3f, %s, %s, %s\n", static_cast<double>(c.t - t_from) / 1e6, c.kind.c_str(), c.us / 1000.0,
                          c.worker ? "worker" : "cp", c.waited ? "waited" : "-", c.name.c_str());
            o << buf;
        }
        std::snprintf(buf, sizeof(buf), "compiles: n=%zu waited=%llu waited_ms=%.1f\n", cs.size(), static_cast<unsigned long long>(n_wait),
                      us_wait / 1000.0);
        o << buf;
    }
    o << "\n[settings]\n" << settings;
    o << "\n[env]\n";
#if defined(__linux__)
    for (char** e = environ; e && *e; ++e)
        if (std::strncmp(*e, "HELIAS_", 7) == 0 || std::strncmp(*e, "MANGOHUD", 8) == 0 || std::strncmp(*e, "RADV_", 5) == 0)
            o << *e << "\n";
#endif
    o << "\n[seconds]  # t_s (from the window's start), area, gpu busy %, the game's command buffers ms/s, the presenter's ms/s, "
         "the busiest threads (CPU ms in that second)\n";
    for (const Second& s : g_seconds) {
        if (s.t < t_from) continue;
        std::snprintf(buf, sizeof(buf), "%.1f, %s, %.1f, %.0f, %.1f,", static_cast<double>(s.t - t_from) / 1e9, block_text(s.block).c_str(),
                      s.gpu_pct, s.game_ms, s.present_ms);
        o << buf;
        for (const auto& [name, ms] : s.threads) {
            std::snprintf(buf, sizeof(buf), " %s=%.0f", name.c_str(), ms);
            o << buf;
        }
        o << "\n";
    }
    o << "\n[frames]  # t_ms (from the window's start), interval_ms, flip_to_present_ms (-1: unknown), dropped (total), "
         "cp_waits (total), rc3: record_ms, submit_ms, pacer_ms, present_call_ms, priority (1: the blit's priority submit)\n";
    for (const Frame& f : frames) {
        std::snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f,%u,%u,%.3f,%.3f,%.3f,%.3f,%d\n", static_cast<double>(f.t - t_from) / 1e6, f.dt_us / 1000.0,
                      f.lat_us >= 0 ? f.lat_us / 1000.0 : -1.0, f.dropped, f.cp_waits, f.rec_us / 1000.0, f.sub_us / 1000.0,
                      f.pace_us / 1000.0, f.pres_us / 1000.0, f.prio ? 1 : 0);
        o << buf;
    }
    o << "\n[log]  # the last 30 s of the log\n";
    for (const auto& [t, line] : lines) {
        std::snprintf(buf, sizeof(buf), "%+.3f ", static_cast<double>(t - t_end) / 1e9);
        o << buf << line << "\n";
    }

    std::error_code ec;
    const std::filesystem::path dir(snap_dir());
    std::filesystem::create_directories(dir, ec);
    std::string tag = block ? block_text(block).substr(0, 12) : std::string("menu");
    const std::string name = "snap-" + local_time("%Y%m%d-%H%M%S") + "-" + tag + ".txt";
    const std::filesystem::path path = dir / name, tmp = dir / (name + ".part");
    {
        std::ofstream f(tmp, std::ios::binary);
        f << o.str();
        if (!f) {
            host_log("snap: could not write %s", tmp.string().c_str());
            set_banner("Snapshot failed: could not write " + dir.string(), 4.0);
            return;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    host_log("snap: %s (%s%s%s): %.1f fps avg, 1%% low %.1f, %d over 9 ms, %u dropped; %s", path.string().c_str(), trigger.c_str(),
             note.empty() ? "" : ": ", note.c_str(), avg_fps, p99 > 0 ? 1000.0 / p99 : 0.0, over9, drops, block_text(block).c_str());
    std::snprintf(buf, sizeof(buf), "Snapshot saved (%.0f fps avg, 1%% low %.0f): %s", avg_fps, p99 > 0 ? 1000.0 / p99 : 0.0, name.c_str());
    set_banner(buf, 3.0);
    rgp_after_snapshot(path, note, block_text(block));
}

// ---- bbtest profile: an RGP capture of the frame after each snapshot ----
// MESA_VK_TRACE=rgp with MESA_VK_TRACE_TRIGGER=<file>: RADV checks the file at
// each present, deletes it and traces the next frame, writing
// /tmp/<process>_<date>_<time>[_frameN].rgp (Mesa's own path, not TMPDIR).
// HELIAS_RGP_DIR (bbtest profile sets it, with the two): the snapshot creates
// the trigger, at most one every 10 s, and a helper thread moves the new .rgp
// beside the snapshot as <snapshot>.rgp, notes it in captures.tsv there and
// in the snapshot's [rgp] section; a trigger the driver never takes or a
// capture that never appears is logged.
struct RgpMode {
    bool on = false;
    std::string trigger, dir;
};
const RgpMode& rgp_mode() {
    static const RgpMode m = [] {
        RgpMode r;
        const char* t = std::getenv("MESA_VK_TRACE");
        const char* f = std::getenv("MESA_VK_TRACE_TRIGGER");
        const char* d = std::getenv("HELIAS_RGP_DIR");
        r.on = t && std::strstr(t, "rgp") && f && *f && d && *d;
        if (r.on) {
            r.trigger = f;
            r.dir = d;
        }
        return r;
    }();
    return m;
}
std::int64_t g_rgp_last = 0;  // the capture thread's own

std::map<std::string, std::uintmax_t> tmp_rgps() {
    std::map<std::string, std::uintmax_t> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it("/tmp", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".rgp") continue;
        std::error_code e2;
        out[it->path().string()] = std::filesystem::file_size(it->path(), e2);
    }
    return out;
}

void append_to(const std::filesystem::path& file, const std::string& text) {
    std::ofstream f(file, std::ios::app | std::ios::binary);
    f << text;
}

void rgp_after_snapshot(const std::filesystem::path& snap, const std::string& note, const std::string& area) {
    const RgpMode& m = rgp_mode();
    if (!m.on) return;
    const std::int64_t now = steady_ns();
    if (g_rgp_last && now - g_rgp_last < 10'000'000'000) {
        const double ago = static_cast<double>(now - g_rgp_last) / 1e9;
        host_log("rgp: no GPU capture for %s: the last one was %.1f s ago (one every 10 s)", snap.filename().string().c_str(), ago);
        append_to(snap, "\n[rgp]\nskipped: the last GPU capture was " + std::to_string(static_cast<int>(ago)) + " s before (one every 10 s)\n");
        return;
    }
    g_rgp_last = now;
    auto before = tmp_rgps();
    {
        std::ofstream t(m.trigger);
        if (!t) {
            host_log("rgp: could not create the trigger file %s", m.trigger.c_str());
            append_to(snap, "\n[rgp]\nfailed: could not create the trigger file " + m.trigger + "\n");
            return;
        }
    }
    const std::string when = local_time("%Y-%m-%d %H:%M:%S %Z");
    host_log("rgp: GPU capture asked for (trigger %s) for %s", m.trigger.c_str(), snap.filename().string().c_str());
    std::thread([m, snap, note, area, when, before = std::move(before)] {
        std::error_code ec;
        const auto t0 = std::chrono::steady_clock::now();
        bool taken = false;
        std::string got;
        std::uintmax_t last_size = 0;
        int stable = 0;
        while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(45)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (!taken) {
                if (!std::filesystem::exists(m.trigger, ec)) {
                    taken = true;
                } else if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(5)) {
                    std::filesystem::remove(m.trigger, ec);
                    host_log("rgp: the driver did not take the trigger in 5 s (is it RADV, with MESA_VK_TRACE=rgp?); removed it");
                    append_to(snap, "\n[rgp]\nfailed: the driver did not take the trigger in 5 s\n");
                    return;
                }
                continue;
            }
            // The newest .rgp in /tmp that was not there before, once its size holds.
            for (const auto& [path, size] : tmp_rgps()) {
                if (before.count(path)) continue;
                if (got != path) {
                    got = path;
                    last_size = size;
                    stable = 0;
                } else if (size == last_size && size > 0) {
                    ++stable;
                } else {
                    last_size = size;
                    stable = 0;
                }
            }
            if (!got.empty() && stable >= 4) break;
        }
        if (got.empty() || stable < 4) {
            host_log("rgp: no capture file appeared in /tmp in 45 s after the driver took the trigger (%s)", snap.filename().string().c_str());
            append_to(snap, "\n[rgp]\nfailed: the driver took the trigger but no /tmp/*.rgp appeared in 45 s\n");
            return;
        }
        std::filesystem::path dst = std::filesystem::path(m.dir) / (snap.stem().string() + ".rgp");
        std::filesystem::create_directories(m.dir, ec);
        std::filesystem::rename(got, dst, ec);
        if (ec) {  // /tmp on another file system: copy, then remove
            ec.clear();
            std::filesystem::copy_file(got, dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                host_log("rgp: could not move %s to %s: %s", got.c_str(), dst.string().c_str(), ec.message().c_str());
                append_to(snap, "\n[rgp]\nfile: " + got + " (could not move it to " + dst.string() + ")\n");
                return;
            }
            std::filesystem::remove(got, ec);
        }
        char mb[32];
        std::snprintf(mb, sizeof(mb), "%.1f", static_cast<double>(last_size) / 1048576.0);
        host_log("rgp: %s (%s MiB) for %s", dst.string().c_str(), mb, snap.filename().string().c_str());
        append_to(snap, "\n[rgp]\nfile: " + dst.filename().string() + " (" + mb + " MiB; the frame after the snapshot was asked for at " + when + ")\n");
        const std::filesystem::path index = std::filesystem::path(m.dir) / "captures.tsv";
        if (!std::filesystem::exists(index, ec)) append_to(index, "# time\trgp\tsnapshot\tarea\tnote\tMiB\n");
        append_to(index, when + "\t" + dst.filename().string() + "\t" + snap.filename().string() + "\t" + area + "\t" + note + "\t" + mb + "\n");
        set_banner("GPU capture saved: " + dst.filename().string(), 3.0);
    }).detach();
}

void thread_main() {
    host_thread_set_name("bb-perfsnap");
    std::map<int, std::pair<std::string, std::uint64_t>> threads;
    GpuBusy busy;
    std::int64_t last_t = 0;
    auto next_second = std::chrono::steady_clock::now();
    for (;;) {
        std::string trigger, note;
        {
            std::unique_lock<std::mutex> lk(g_req_mu);
            g_req_cv.wait_for(lk, std::chrono::milliseconds(250), [] { return g_req; });
            if (g_req) {
                g_req = false;
                trigger = g_req_trigger;
                note = g_req_note;
            }
        }
        if (std::chrono::steady_clock::now() >= next_second) {
            next_second += std::chrono::seconds(1);
            if (std::chrono::steady_clock::now() > next_second) next_second = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            tick_second(threads, busy, last_t);
        }
        if (trigger.empty()) {  // the request file (bbtest snap)
            std::error_code ec;
            const std::filesystem::path req = std::filesystem::path(snap_dir()) / "snap.request";
            if (std::filesystem::exists(req, ec)) {
                std::ifstream f(req);
                std::getline(f, note);
                f.close();
                std::filesystem::remove(req, ec);
                trigger = "bbtest snap";
            }
        }
        if (!trigger.empty()) capture(trigger, note);
    }
}

}  // namespace

bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("HELIAS_SNAP");
        return !(e && e[0] == '0');
    }();
    return on;
}

void start() {
    if (!enabled() || g_started.exchange(true)) return;
    g_log = new LogSlot[kLogSlots];
    g_frames = new std::array<Frame, kFrames>();
    g_host_log_tap = log_tap;
    std::thread(thread_main).detach();
    host_log("snap: F11 or `bbtest snap \"note\"` saves the last 30 s (frame times, GPU passes, post at render, settings, log) to %s/",
             snap_dir().c_str());
}


void add_section(const char* title, SectionFn fn) {
    std::lock_guard<std::mutex> lk(g_sections_mu);
    for (const auto& s : sections())
        if (s.second == fn) return;
    sections().emplace_back(title, fn);
}

void note_present(std::int64_t now_ns, std::int64_t arrived_ns, std::uint64_t dropped_total, std::uint64_t cp_waits_total,
                  std::uint64_t cp_wait_ns_total, const PresentSplit& split) {
    if (!g_frames) return;
    const std::int64_t last = g_last_present.exchange(now_ns, std::memory_order_relaxed);
    const std::int64_t dt = last ? now_ns - last : 0;
    g_last_interval.store(dt, std::memory_order_relaxed);
    Frame f;
    f.t = now_ns;
    f.dt_us = static_cast<std::int32_t>(std::min<std::int64_t>(dt / 1000, 2'000'000'000));
    f.lat_us = arrived_ns > 0 && now_ns > arrived_ns ? static_cast<std::int32_t>(std::min<std::int64_t>((now_ns - arrived_ns) / 1000, 2'000'000'000)) : -1;
    f.dropped = static_cast<std::uint32_t>(dropped_total);
    f.cp_waits = static_cast<std::uint32_t>(cp_waits_total);
    f.cp_wait_ns = cp_wait_ns_total;
    f.rec_us = split_us(split.record_ns);
    f.sub_us = split_us(split.submit_ns);
    f.pace_us = split_us(split.pace_ns);
    f.pres_us = split_us(split.present_ns);
    f.prio = split.priority;
    std::lock_guard<std::mutex> lk(g_frames_mu);
    (*g_frames)[g_frame_n % kFrames] = f;
    ++g_frame_n;
}

std::int64_t last_interval_ns() { return g_last_interval.load(std::memory_order_relaxed); }

void note_gpu_sample(std::uint64_t flip, double span_ms, double timed_ms, std::size_t entries,
                     const std::vector<std::pair<std::string, double>>& top) {
    Sample s;
    s.t = steady_ns();
    s.flip = flip;
    s.span_ms = span_ms;
    s.timed_ms = timed_ms;
    s.entries = entries;
    s.top = top;
    std::lock_guard<std::mutex> lk(g_samples_mu);
    g_samples.push_back(std::move(s));
    while (g_samples.size() > 8) g_samples.pop_front();
}

void request(const char* trigger, const std::string& note) {
    if (!enabled() || !g_started.load()) return;
    {
        std::lock_guard<std::mutex> lk(g_req_mu);
        g_req = true;
        g_req_trigger = trigger ? trigger : "?";
        g_req_note = note;
    }
    g_req_cv.notify_one();
    set_banner("Saving a snapshot of the last 30 s...", 3.0);
}

std::string banner() {
    std::lock_guard<std::mutex> lk(g_banner_mu);
    return steady_ns() < g_banner_until ? g_banner : std::string();
}

std::atomic<std::uint64_t> g_last_compile_ns{0};

bool compiling_recently(std::uint64_t within_ns) {
    const std::uint64_t t = g_last_compile_ns.load(std::memory_order_relaxed);
    return t && static_cast<std::uint64_t>(steady_ns()) - t < within_ns;
}

void note_compile(const char* kind, const std::string& name, std::uint64_t us, bool worker, bool waited) {
    // Only one a frame waited for: the workers building ahead is not felt.
    if (waited) g_last_compile_ns.store(static_cast<std::uint64_t>(steady_ns()), std::memory_order_relaxed);
    const int k = kind[0] == 't' ? 0 : kind[0] == 'g' ? 2 : 4;
    g_ct[k + (worker ? 0 : 1)].fetch_add(1, std::memory_order_relaxed);
    if (waited) {
        g_ct_waited.fetch_add(1, std::memory_order_relaxed);
        g_ct_waited_us.fetch_add(us, std::memory_order_relaxed);
    }
    if (!g_started.load(std::memory_order_relaxed)) return;
    Compile c;
    c.t = steady_ns();
    c.kind = kind;
    c.name = name;
    c.us = us;
    c.worker = worker;
    c.waited = waited;
    std::lock_guard<std::mutex> lk(g_compiles_mu);
    g_compiles.push_back(std::move(c));
    while (g_compiles.size() > 4096) g_compiles.pop_front();
}

void set_worker_thread() { t_worker = true; }
bool on_worker_thread() { return t_worker; }

CompileTotals compile_totals() {
    CompileTotals t;
    t.translate_worker = g_ct[0].load();
    t.translate_cp = g_ct[1].load();
    t.graphics_worker = g_ct[2].load();
    t.graphics_cp = g_ct[3].load();
    t.compute_worker = g_ct[4].load();
    t.compute_cp = g_ct[5].load();
    t.waited = g_ct_waited.load();
    t.waited_us = g_ct_waited_us.load();
    return t;
}

// The translation cache's hook, from the start (the totals count the boot's).
namespace {
const bool g_hook_set = [] {
    g_translation_compile_hook = [](const char* what, std::uint64_t us) { note_compile("translate", what, us, on_worker_thread(), !on_worker_thread()); };
    return true;
}();
}  // namespace

bool post_wanted() { return g_post_want.load(std::memory_order_relaxed); }

void publish_post(const std::string& text) {
    {
        std::lock_guard<std::mutex> lk(g_post_mu);
        g_post_text = text;
        g_post_t = steady_ns();
    }
    g_post_want.store(false);
}

}  // namespace perf
