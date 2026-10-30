// Profiling kit: the PGO-instrumented build's profile writer (host/pgo_dump.h).
// Only compiled with -DHELIAS_PGO_GENERATE=ON.

#include "host/pgo_dump.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <time.h>
#include <unistd.h>

#include "core/portable.h"
#include "log.h"

extern "C" {
// compiler-rt's profile runtime (linked by -fprofile-generate).
int __llvm_profile_write_file(void);
void __llvm_profile_set_filename(const char* name);
}

namespace {
std::atomic<bool> g_started{false};
std::atomic<int> g_final{0};  // 1: asked, 2: written
std::string g_final_path, g_tmp_path;
std::atomic<unsigned> g_writes{0};

void write_now(const char* why) {
    const auto t0 = std::chrono::steady_clock::now();
    __llvm_profile_set_filename(g_tmp_path.c_str());
    const int r = __llvm_profile_write_file();
    // The runtime's own exit-time write (std::exit paths) goes to the final name.
    __llvm_profile_set_filename(g_final_path.c_str());
    std::error_code ec;
    if (r == 0) std::filesystem::rename(g_tmp_path, g_final_path, ec);
    const unsigned n = g_writes.fetch_add(1) + 1;
    if (n <= 3 || r != 0 || ec || why[0] == 'f') {
        host_log("pgo: profile written (%s, #%u, %.0f ms)%s%s -> %s", why, n,
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), r ? " FAILED" : "",
                 ec ? " (rename failed)" : "", g_final_path.c_str());
    }
}
}  // namespace

void pgo_dump_start() {
    if (g_started.exchange(true)) return;
    const char* d = std::getenv("HELIAS_PGO_DIR");
    std::string dir = d && *d ? d : "pgo";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path abs = std::filesystem::absolute(dir, ec);
    if (!ec) dir = abs.string();
    const long pid = static_cast<long>(::getpid());
    g_final_path = dir + "/heliasdream-" + std::to_string(pid) + ".profraw";
    g_tmp_path = g_final_path + ".tmp";
    __llvm_profile_set_filename(g_final_path.c_str());
    const char* p = std::getenv("HELIAS_PGO_DUMP_S");
    const int period_s = p ? std::max(5, std::atoi(p)) : 30;
    host_log("pgo: PGO-instrumented build (not for play: the CPU side runs slower); profile every %d s and at exit -> %s", period_s,
             g_final_path.c_str());
    std::thread([period_s] {
        host_thread_set_name("bb-pgo");
        auto next = std::chrono::steady_clock::now() + std::chrono::seconds(period_s);
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (g_final.load() == 1) {
                write_now("final");
                g_final.store(2);
                return;
            }
            if (std::chrono::steady_clock::now() >= next) {
                write_now("periodic");
                next = std::chrono::steady_clock::now() + std::chrono::seconds(period_s);
            }
        }
    }).detach();
}

void pgo_dump_final() {
    if (!g_started.load()) return;
    int expect = 0;
    g_final.compare_exchange_strong(expect, 1);
    for (int i = 0; i < 50 && g_final.load() != 2; ++i) {
        struct timespec ts{0, 100'000'000};
        nanosleep(&ts, nullptr);
    }
}
