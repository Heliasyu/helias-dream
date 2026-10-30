// host/flip_trace.cpp (HELIAS_FLIP_TRACE=1, set by ctest): 60 simulated flips
// through the game -> command processor -> present hooks, one of them 14 ms
// late. The trace must log exactly one `fliptrace: slow frame` (with the flip
// before it), and its snapshot section must have a CSV line per presented
// flip with the late one's interval and its CP walk.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "host/flip_trace.h"
#include "host/perf_capture.h"
#include "log.h"

std::uint64_t hle_gnm_cp_backlog() { return 3; }
namespace perf {
SectionFn g_fn = nullptr;
std::string g_title;
void add_section(const char* title, SectionFn fn) {
    g_title = title;
    g_fn = fn;
}
}  // namespace perf

namespace {
std::mutex g_lines_mu;
std::vector<std::string> g_lines;
void tap(const char* line, std::size_t len) {
    std::lock_guard<std::mutex> lk(g_lines_mu);
    g_lines.emplace_back(line, len);
}
std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_fail;
}
}  // namespace

int main() {
    g_host_log_tap = tap;
    check(fliptrace::on(), "HELIAS_FLIP_TRACE=1 turns it on");
    check(perf::g_fn && perf::g_title == "flip_trace", "the [flip_trace] snapshot section is registered");
    for (int i = 1; i <= 60; ++i) {
        fliptrace::main_frame(7000);
        const std::uint64_t id = fliptrace::submit_begin();
        fliptrace::submit_end(id);
        fliptrace::cp_job_begin();  // a job before the flip's
        fliptrace::cp_job_end();
        fliptrace::cp_job_begin();  // the flip's job
        std::this_thread::sleep_for(std::chrono::microseconds(i == 30 ? 13000 : 7000));
        fliptrace::cp_job_end();
        fliptrace::cp_flip(id, false);
        const std::int64_t arrived = now_ns();
        fliptrace::cp_flip(id, true);
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        fliptrace::present(now_ns(), arrived);
    }
    int slow = 0, before = 0;
    for (const std::string& l : g_lines) {
        slow += l.find("fliptrace: slow frame") != std::string::npos;
        before += l.find("fliptrace:   the flip before: flip 29") != std::string::npos;
        if (l.find("fliptrace:") != std::string::npos) std::fputs(l.c_str(), stdout);
    }
    check(slow == 1, "one slow frame logged");
    check(before == 1, "with the flip before it");
    const std::string sec = perf::g_fn ? perf::g_fn() : std::string();
    int rows = 0;
    bool late = false;
    for (std::size_t a = 0, b; a < sec.size(); a = b + 1) {
        b = sec.find('\n', a);
        if (b == std::string::npos) b = sec.size();
        const std::string row = sec.substr(a, b - a);
        if (row.empty() || row[0] == '#') continue;
        ++rows;
        unsigned long long id = 0;
        double t = 0, interval = 0, gap = 0, mainf = 0, m2s = 0, bp = 0, q = 0, walk = 0;
        unsigned backlog = 0;
        if (std::sscanf(row.c_str(), "%llu,%lf,%lf,%lf,%lf,%lf,%lf,%u,%lf,%lf", &id, &t, &interval, &gap, &mainf, &m2s, &bp, &backlog, &q, &walk) == 10 &&
            id == 30) {
            late = interval > 11.0 && walk > 12.0 && backlog == 3 && mainf > 6.9;
        }
    }
    check(rows == 60, "a CSV line per presented flip");
    check(late, "the late flip's interval and CP walk in its line");
    std::printf("%s\n", g_fail ? "FAILED" : "passed");
    return g_fail ? 1 : 0;
}
