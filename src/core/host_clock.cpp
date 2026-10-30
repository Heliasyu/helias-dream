#include "core/host_clock.h"
#include "log.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <timeapi.h>
#endif
#if defined(__linux__)
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

bool env_off(const char* name) {
    const char* e = std::getenv(name);
    return e && e[0] == '0';
}

#if defined(_WIN32)
// QueryPerformanceCounter ticks to nanoseconds and to other rates. The
// frequency is fixed at boot (10 MHz on Windows 10 and later, where QPC runs
// off the invariant TSC through the shared user page), so the scale is set
// up once: a whole multiplier when the rate divides a second evenly, else a
// 32.32 fixed-point one through a 128-bit product.
struct QpcScale {
    std::uint64_t freq = 1;
    std::uint64_t ns_whole = 0;  // ns per tick, when exact
    std::uint64_t ns_fixed = 0;  // (1e9 << 32) / freq otherwise
};
QpcScale make_qpc_scale() {
    QpcScale s;
    LARGE_INTEGER f;
    s.freq = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? static_cast<std::uint64_t>(f.QuadPart) : 10000000ull;
    if (1000000000ull % s.freq == 0) {
        s.ns_whole = 1000000000ull / s.freq;
    } else {
        s.ns_fixed = static_cast<std::uint64_t>((static_cast<unsigned __int128>(1000000000ull) << 32) / s.freq);
    }
    return s;
}
// A function's static, not a namespace one: the first reader sets it up
// whatever the order of static initialization (host_timing_init reads it
// first, at the start of main).
const QpcScale& qpc() {
    static const QpcScale s = make_qpc_scale();
    return s;
}

inline std::uint64_t qpc_now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<std::uint64_t>(c.QuadPart);
}

// Ticks to `hz`, exact for any whole number of seconds and the remainder in
// one 128-bit multiply-divide; no 64-bit overflow for centuries of uptime.
inline std::uint64_t qpc_scale_to(std::uint64_t ticks, std::uint64_t hz) {
    const std::uint64_t f = qpc().freq;
    const std::uint64_t sec = ticks / f, rem = ticks % f;
    return sec * hz + static_cast<std::uint64_t>(static_cast<unsigned __int128>(rem) * hz / f);
}
#endif

}  // namespace

std::uint64_t host_clock_monotonic_ns() {
#if defined(_WIN32)
    const QpcScale& s = qpc();
    const std::uint64_t t = qpc_now();
    if (s.ns_whole) return t * s.ns_whole;
    return static_cast<std::uint64_t>((static_cast<unsigned __int128>(t) * s.ns_fixed) >> 32);
#else
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(ts.tv_nsec);
#endif
}

std::int64_t host_clock_realtime_ns() {
#if defined(_WIN32)
    // 100 ns units since 1601; the precise form reads the same counter as
    // QPC plus the system's offset (no system call on Windows 8 and later).
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    const std::uint64_t t = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    constexpr std::uint64_t kUnixEpoch = 116444736000000000ull;
    return static_cast<std::int64_t>(t - kUnixEpoch) * 100;
#else
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1000000000ll + static_cast<std::int64_t>(ts.tv_nsec);
#endif
}

std::uint64_t host_clock_ticks_at(std::uint64_t hz) {
#if defined(_WIN32)
    const std::uint64_t f = qpc().freq;
    if (hz == f) return qpc_now();
    if (hz % f == 0) return qpc_now() * (hz / f);  // 1.6 GHz from 10 MHz: x160
    return qpc_scale_to(qpc_now(), hz);
#else
    const std::uint64_t ns = host_clock_monotonic_ns();
    return ns / 1000000000ull * hz + static_cast<std::uint64_t>(static_cast<unsigned __int128>(ns % 1000000000ull) * hz / 1000000000ull);
#endif
}

void host_timing_init() {
#if defined(__linux__)
    // rc5: the process's own CPU mask and its L3 domains, read before any
    // thread places itself (host_thread_place).
    (void)host_cpu_placement_section();
#endif
#if defined(_WIN32)
    // 1 ms timer resolution for the whole run. host_sleep_us's waitable
    // timers are high-resolution and do not need it; Sleep, the timed waits
    // (WaitOnAddress, winpthreads' condition variables) and the driver's own
    // waits do.
    const bool period = !env_off("HELIAS_TIMER_PERIOD");
    if (period) timeBeginPeriod(1);
    // Power throttling off for the process (HighQoS): EXECUTION_SPEED keeps
    // Windows from running its threads at efficiency clocks (EcoQoS) when it
    // judges the window unimportant, IGNORE_TIMER_RESOLUTION keeps the 1 ms
    // above honoured while the window is hidden or minimized (Windows 11
    // drops it otherwise). Looked up by name: SetProcessInformation is
    // Windows 8 and later, and older SDK headers lack the flags.
    bool qos = false;
    if (!env_off("HELIAS_WIN_QOS")) {
        struct PowerThrottling {
            ULONG version, control_mask, state_mask;
        };
        constexpr ULONG kExecutionSpeed = 0x1, kIgnoreTimerResolution = 0x4;
        constexpr int kProcessPowerThrottling = 4;  // PROCESS_INFORMATION_CLASS
        using SetInfo = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        const auto set_info = k ? reinterpret_cast<SetInfo>(reinterpret_cast<void*>(GetProcAddress(k, "SetProcessInformation"))) : nullptr;
        if (set_info) {
            PowerThrottling s{1, kExecutionSpeed | kIgnoreTimerResolution, 0};
            qos = set_info(GetCurrentProcess(), kProcessPowerThrottling, &s, sizeof(s)) != 0;
            if (!qos) {
                // Before Windows 11 the timer-resolution flag is unknown and
                // the call fails: the execution-speed half alone.
                s.control_mask = kExecutionSpeed;
                qos = set_info(GetCurrentProcess(), kProcessPowerThrottling, &s, sizeof(s)) != 0;
            }
        }
    }
    // What the system timer runs at now (100 ns units), from ntdll.
    using QueryRes = LONG(NTAPI*)(PULONG, PULONG, PULONG);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    const auto query = nt ? reinterpret_cast<QueryRes>(reinterpret_cast<void*>(GetProcAddress(nt, "NtQueryTimerResolution"))) : nullptr;
    ULONG coarsest = 0, finest = 0, now = 0;
    const bool have_res = query && query(&coarsest, &finest, &now) == 0;
    host_log("timing: timer resolution %s (system now %.3f ms, finest %.3f ms); power throttling %s; QPC %llu Hz",
             period ? "1 ms asked at start" : "left to the system (HELIAS_TIMER_PERIOD=0)",
             have_res ? now / 10000.0 : 0.0, have_res ? finest / 10000.0 : 0.0,
             env_off("HELIAS_WIN_QOS") ? "left on (HELIAS_WIN_QOS=0)" : (qos ? "off for this process (HighQoS, resolution kept when hidden)" : "could not be switched off"),
             static_cast<unsigned long long>(qpc().freq));
#endif
}

#if defined(__linux__)
namespace {

// Graphics threads first (host_threads_set_priority): the hot threads met so
// far, each with the nice value it gets when the setting is on.
struct PrioThread {
    pid_t tid;
    int nice;
    std::string who;
};
std::mutex g_prio_mu;
std::vector<PrioThread> g_prio_threads;  // under g_prio_mu
bool g_prio_on = false;                  // under g_prio_mu

// The lowest nice value RLIMIT_NICE lets this process take (20 - the limit).
int prio_floor() {
    static const int f = [] {
        rlimit r{};
        if (getrlimit(RLIMIT_NICE, &r) != 0) return 0;
        const long long cur = r.rlim_cur == RLIM_INFINITY ? 40 : static_cast<long long>(r.rlim_cur);
        return static_cast<int>(std::clamp(20 - cur, -20ll, 20ll));
    }();
    return f;
}

// Sets one thread's nice value; the tid is checked to still be ours (a
// thread that ended may have had its id reused).
void prio_apply_locked(const PrioThread& t, bool on) {
    if (syscall(SYS_tgkill, getpid(), t.tid, 0) != 0) return;
    const int want = on ? std::max(t.nice, prio_floor()) : 0;
    if (on && want >= 0) return;
    setpriority(PRIO_PROCESS, static_cast<id_t>(t.tid), want);
}

void prio_register(int nice, const char* who) {
    const pid_t tid = static_cast<pid_t>(syscall(SYS_gettid));
    std::lock_guard<std::mutex> lk(g_prio_mu);
    PrioThread* t = nullptr;
    for (PrioThread& e : g_prio_threads)
        if (e.tid == tid) t = &e;
    if (!t) {
        g_prio_threads.push_back({tid, nice, who ? who : "a thread"});
        t = &g_prio_threads.back();
    } else {
        t->nice = std::min(t->nice, nice);
    }
    if (g_prio_on) prio_apply_locked(*t, true);
}

}  // namespace

void host_threads_set_priority(bool on) {
    std::lock_guard<std::mutex> lk(g_prio_mu);
    if (on == g_prio_on) return;
    g_prio_on = on;
    if (on && prio_floor() >= 0) {
        host_log("priority: Graphics threads first is on, but this system does not let a program raise its threads' priority "
                 "(RLIMIT_NICE, ulimit -e, is 0): nothing changed");
        return;
    }
    for (const PrioThread& t : g_prio_threads) prio_apply_locked(t, on);
    if (on) {
        host_log("priority: Graphics threads first: %zu hot threads so far at nice -5 (the vblank clock -10), floored at %d by RLIMIT_NICE",
                 g_prio_threads.size(), prio_floor());
    } else {
        host_log("priority: Graphics threads first off: %zu hot threads back at nice 0", g_prio_threads.size());
    }
}
#else
void host_threads_set_priority(bool) {}
#endif

void host_thread_set_class(HostThreadClass c, const char* who) {
    static const bool on = !env_off("HELIAS_THREAD_PRIO");
#if defined(_WIN32)
    const char* const level = c == HostThreadClass::Pacing ? "highest" : "above-normal";
    if (!on) {
        static std::atomic<bool> said{false};
        if (!said.exchange(true)) host_log("timing: the pacing threads run at normal priority (HELIAS_THREAD_PRIO=0)");
        return;
    }
    const int prio = c == HostThreadClass::Pacing ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_ABOVE_NORMAL;
    // Each of these threads starts once (a command processor per queue), so
    // one line each.
    if (SetThreadPriority(GetCurrentThread(), prio)) {
        host_log("timing: %s runs at %s priority (HELIAS_THREAD_PRIO=0: normal)", who, level);
    } else {
        host_log("timing: %s could not be set to %s priority (error %lu)", who, level, static_cast<unsigned long>(GetLastError()));
    }
#else
    (void)on;
    (void)c;
    // rc5: Linux has no priority here (that needs CAP_SYS_NICE); the timer
    // slack and the placement instead.
    host_thread_tight_timers(who);
    host_thread_place(HostPlace::Hot, who);
    if (c == HostThreadClass::Pacing) prio_register(-10, who);
#endif
}

#if defined(__linux__)
namespace {

std::atomic<int> g_slack_set{0}, g_slack_failed{0};
std::atomic<bool> g_slack_on{false};

// The placement: the process's own mask, taken at start (host_timing_init)
// before any thread changed its own, and the hot and cold masks made from it.
struct Placement {
    bool on = false;
    std::string why = "not looked at yet";
    cpu_set_t any{}, hot{}, cold{};
    std::string hot_list, cold_list, by;
    int domains = 0;
};
std::atomic<int> g_placed[3] = {}, g_place_failed{0};
std::mutex g_place_log_mu;
int g_place_logs = 0;

std::string cpu_list(const cpu_set_t& s) {
    std::string out;
    for (int c = 0; c < CPU_SETSIZE; ++c) {
        if (!CPU_ISSET(c, &s)) continue;
        int e = c;
        while (e + 1 < CPU_SETSIZE && CPU_ISSET(e + 1, &s)) ++e;
        if (!out.empty()) out += ',';
        out += std::to_string(c);
        if (e > c) out += '-' + std::to_string(e);
        c = e;
    }
    return out;
}

bool read_line(const char* path, std::string& out) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char buf[512];
    const bool ok = std::fgets(buf, sizeof(buf), f) != nullptr;
    std::fclose(f);
    if (!ok) return false;
    out = buf;
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return true;
}

long long read_number(const char* path) {
    std::string l;
    if (!read_line(path, l) || l.empty()) return -1;
    char* end = nullptr;
    long long v = std::strtoll(l.c_str(), &end, 10);
    // "32768K" (cache sizes)
    if (end && (*end == 'K' || *end == 'k')) v *= 1024;
    else if (end && (*end == 'M' || *end == 'm')) v *= 1024 * 1024;
    return v;
}

bool parse_list(const std::string& l, cpu_set_t& out) {
    CPU_ZERO(&out);
    const char* p = l.c_str();
    bool any = false;
    while (*p) {
        char* end = nullptr;
        const long a = std::strtol(p, &end, 10);
        if (end == p) return false;
        long b = a;
        p = end;
        if (*p == '-') {
            b = std::strtol(p + 1, &end, 10);
            if (end == p + 1) return false;
            p = end;
        }
        for (long c = a; c <= b && c < CPU_SETSIZE; ++c) {
            if (c >= 0) CPU_SET(static_cast<int>(c), &out);
            any = true;
        }
        if (*p == ',') ++p;
        else if (*p) return false;
    }
    return any;
}

Placement& placement() {
    static Placement pl = [] {
        Placement p;
        if (env_off("HELIAS_AFFINITY")) {
            p.why = "HELIAS_AFFINITY=0";
            return p;
        }
        if (sched_getaffinity(0, sizeof(p.any), &p.any) != 0) {
            p.why = "the process's mask could not be read";
            return p;
        }
        // The L3 domains of the CPUs the process may run on.
        struct Domain {
            cpu_set_t cpus;
            long long l3 = -1, rank = -1;
            int first = CPU_SETSIZE;
        };
        std::vector<Domain> ds;
        char path[160];
        bool ranked = false, cppc = false, freq = false;
        for (int c = 0; c < CPU_SETSIZE; ++c) {
            if (!CPU_ISSET(c, &p.any)) continue;
            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cache/index3/shared_cpu_list", c);
            std::string l;
            cpu_set_t set;
            if (!read_line(path, l) || !parse_list(l, set)) {
                p.why = "no L3 sharing list in sysfs";
                return p;
            }
            CPU_AND(&set, &set, &p.any);
            Domain* d = nullptr;
            for (Domain& x : ds) {
                if (CPU_EQUAL(&x.cpus, &set)) d = &x;
            }
            if (!d) {
                ds.push_back(Domain{});
                d = &ds.back();
                d->cpus = set;
                std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cache/index3/size", c);
                d->l3 = read_number(path);
            }
            d->first = std::min(d->first, c);
            // Preferred cores (amd-pstate), else the CPPC highest performance,
            // else the highest frequency: the domain's best core.
            long long r = -1;
            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/amd_pstate_prefcore_ranking", c);
            if ((r = read_number(path)) >= 0) {
                ranked = true;
            } else {
                std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/acpi_cppc/highest_perf", c);
                if ((r = read_number(path)) >= 0) {
                    cppc = true;
                } else {
                    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
                    if ((r = read_number(path)) >= 0) freq = true;
                }
            }
            d->rank = std::max(d->rank, r);
        }
        p.domains = static_cast<int>(ds.size());
        if (ds.size() < 2) {
            p.why = ds.empty() ? "no CPUs" : "one L3 domain (nothing to choose)";
            return p;
        }
        for (const Domain& d : ds) {
            if (CPU_COUNT(&d.cpus) < 6) {
                p.why = "an L3 domain with fewer than 6 CPUs";
                return p;
            }
        }
        // The larger L3 first (a 3D V-Cache die), then the best core, then
        // the lowest-numbered.
        const Domain* best = &ds[0];
        for (const Domain& d : ds) {
            if (d.l3 > best->l3 || (d.l3 == best->l3 && (d.rank > best->rank || (d.rank == best->rank && d.first < best->first)))) best = &d;
        }
        p.hot = best->cpus;
        CPU_ZERO(&p.cold);
        for (int c = 0; c < CPU_SETSIZE; ++c) {
            if (CPU_ISSET(c, &p.any) && !CPU_ISSET(c, &p.hot)) CPU_SET(c, &p.cold);
        }
        p.hot_list = cpu_list(p.hot);
        p.cold_list = cpu_list(p.cold);
        p.by = ranked ? "prefcore ranking" : cppc ? "CPPC highest perf" : freq ? "max frequency" : "lowest CPU";
        p.on = true;
        p.why = "on";
        return p;
    }();
    return pl;
}

}  // namespace

bool host_thread_tight_timers(const char* who) {
    static const bool on = !env_off("HELIAS_TIMER_SLACK");
    if (!on) return false;
    thread_local bool done = false, ok = false;
    if (done) return ok;
    done = true;
    ok = prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL) == 0;
    (ok ? g_slack_set : g_slack_failed).fetch_add(1, std::memory_order_relaxed);
    if (ok) g_slack_on.store(true, std::memory_order_relaxed);
    if (g_slack_set.load() + g_slack_failed.load() <= 16) {
        host_log("timer slack: %s %s (HELIAS_TIMER_SLACK=0 leaves the default 50 us)", who ? who : "a thread",
                 ok ? "set to 1 ns" : "could not be set");
    }
    return ok;
}

bool host_tight_timers_on() { return g_slack_on.load(std::memory_order_relaxed); }

void host_thread_place(HostPlace where, const char* who) {
    if (where == HostPlace::Hot) prio_register(-5, who);
    Placement& p = placement();
    if (!p.on) {
        static std::atomic<bool> said{false};
        if (!said.exchange(true)) host_log("placement: off: %s", p.why.c_str());
        return;
    }
    const cpu_set_t& m = where == HostPlace::Hot ? p.hot : where == HostPlace::Cold ? p.cold : p.any;
    const bool ok = sched_setaffinity(0, sizeof(m), &m) == 0;
    if (ok) g_placed[static_cast<int>(where)].fetch_add(1, std::memory_order_relaxed);
    else g_place_failed.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_place_log_mu);
    static bool said_masks = false;
    if (!said_masks) {
        said_masks = true;
        host_log("placement: %d L3 domains; the hot chain on CPUs %s, the shader compilers on %s in game (chosen by %s; "
                 "HELIAS_AFFINITY=0 turns it off)",
                 p.domains, p.hot_list.c_str(), p.cold_list.c_str(), p.by.c_str());
    }
    static std::vector<std::string> named;
    const std::string n = who ? who : "a thread";
    if (where != HostPlace::Any && g_place_logs < 24 && std::find(named.begin(), named.end(), n) == named.end()) {
        named.push_back(n);
        ++g_place_logs;
        host_log("placement: %s on the %s CPUs%s", who ? who : "a thread", where == HostPlace::Hot ? "hot" : "cold",
                 ok ? "" : " - refused (ignored)");
    }
}

std::string host_cpu_placement_section() {
    const Placement& p = placement();
    char b[512];
    std::snprintf(b, sizeof(b),
                  "placement: on=%d domains=%d hot=%s cold=%s by=%s hot_threads=%d cold_threads=%d reset_threads=%d failed=%d%s%s "
                  "(rc5: HELIAS_AFFINITY=0 turns it off)\n"
                  "timer_slack: threads=%d failed=%d (rc5: 1 ns; HELIAS_TIMER_SLACK=0 leaves 50 us)",
                  p.on ? 1 : 0, p.domains, p.on ? p.hot_list.c_str() : "-", p.on ? p.cold_list.c_str() : "-",
                  p.on ? p.by.c_str() : "-", g_placed[0].load(), g_placed[1].load(), g_placed[2].load(), g_place_failed.load(),
                  p.on ? "" : " why=", p.on ? "" : p.why.c_str(), g_slack_set.load(), g_slack_failed.load());
    return b;
}
#else
bool host_thread_tight_timers(const char*) { return false; }
bool host_tight_timers_on() { return false; }
void host_thread_place(HostPlace, const char*) {}
std::string host_cpu_placement_section() { return "placement: on=0 (not Linux)\ntimer_slack: threads=0 failed=0 (not Linux)"; }
#endif
