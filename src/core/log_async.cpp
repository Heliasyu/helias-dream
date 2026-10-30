// rc6: asynchronous logging (log.h). host_log used to write(2) each line on
// the thread that logged it, so a slow log file (game.log on btrfs during
// writeback, a full tee pipe) stalled whichever thread was logging: the rc5
// profile caught GXRenderThread in a synchronous write for 3-5 ms. Lines now go
// into a fixed ring and one writer thread ("bb-log") writes them, in order, in
// batches. A producer never sleeps: it reserves slots with one CAS and copies
// its line; only a full ring (a log storm the writer cannot keep up with) makes
// it drain the ring itself, as the old synchronous write would have stalled it.
//
// Order: lines take consecutive tickets and the writer emits them in ticket
// order, so lines keep the order they were logged in (as before, the order in
// which threads reached the reservation). Contents are unchanged: the same
// bytes, one line per write boundary (a batch never ends inside a line).
//
// The crash and exit paths call host_log_sync_mode()/host_log_flush(): what is
// queued is written first, then every later line synchronously. A process
// killed outright (SIGKILL, OOM) loses at most the lines of the last few
// milliseconds. HELIAS_LOG_SYNC=1: the old synchronous writes.
//
// The producer side runs on any thread, including guest threads inside HLE
// calls and the fault handler: no thread-locals, no locks, no libc calls that
// touch errno (the futex wake is a raw syscall), no stack protector.
#include "log.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#if !defined(_WIN32)
#include <linux/futex.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr std::size_t kSlotBytes = 244;
constexpr std::uint64_t kSlots = 16384;  // 4 MiB; a line of up to 1 KiB takes 1-5 slots
constexpr std::uint64_t kMask = kSlots - 1;

struct alignas(256) Slot {
    std::atomic<std::uint64_t> seq{0};  // ticket + 1 once the chunk is written
    std::uint16_t len = 0;
    std::uint8_t last = 0;  // the line's final chunk
    char data[kSlotBytes];
};
static_assert(sizeof(Slot) == 256, "one slot, four cache lines");

Slot g_ring[kSlots];
alignas(64) std::atomic<std::uint64_t> g_head{0};
alignas(64) std::atomic<std::uint64_t> g_tail{0};
alignas(64) std::atomic<int> g_drain_lock{0};
std::atomic<std::uint32_t> g_futex{0};
std::atomic<int> g_sleeping{0};
std::atomic<std::uint64_t> g_lines{0}, g_self_drains{0}, g_overflow{0}, g_deepest{0};
std::atomic<bool> g_started{false};

__attribute__((no_stack_protector)) inline long raw_syscall3(long n, long a, long b, long c) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return ret;
}

__attribute__((no_stack_protector)) inline void raw_yield() {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(static_cast<long>(SYS_sched_yield)) : "rcx", "r11", "memory");
    (void)ret;
}

__attribute__((no_stack_protector)) inline void raw_sleep_us(long us) {
    struct timespec ts{0, us * 1000};
    raw_syscall3(SYS_nanosleep, reinterpret_cast<long>(&ts), 0, 0);
}

std::uint64_t now_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(ts.tv_nsec);
}

// Under g_drain_lock. Writes every chunk published in ticket order; a ticket
// reserved but not yet written stops the drain unless `wait_until` (ns) lets
// it wait for it. Returns the chunks written.
// Batches of whole lines, at most PIPE_BUF (4096) bytes: a write to a pipe
// (run-heliasdream.sh's tee) of up to that many bytes is atomic, so another
// writer's output (the driver's, MangoHud's) never lands inside a line, as
// with the old one-line writes.
constexpr std::size_t kBatchFlush = 4096 - 5 * kSlotBytes;
char g_batch[8192];
__attribute__((no_stack_protector)) std::uint64_t drain_locked(std::uint64_t wait_until) {
    std::uint64_t t = g_tail.load(std::memory_order_relaxed), done = 0;
    std::size_t fill = 0, line_end = 0;
    for (;;) {
        Slot& s = g_ring[t & kMask];
        if (s.seq.load(std::memory_order_acquire) != t + 1) {
            if (g_head.load(std::memory_order_acquire) == t) break;  // nothing more reserved
            if (!wait_until || now_ns() >= wait_until) break;       // a producer is mid-copy
            raw_yield();
            continue;
        }
        std::memcpy(g_batch + fill, s.data, s.len);
        fill += s.len;
        if (s.last) line_end = fill;
        ++t;
        ++done;
        g_tail.store(t, std::memory_order_release);
        if (line_end > kBatchFlush) {
            host_log_write_fd2(g_batch, line_end);
            std::memmove(g_batch, g_batch + line_end, fill - line_end);
            fill -= line_end;
            line_end = 0;
        }
    }
    // Whole lines only: a chunk left over (its line's later chunks still being
    // copied) goes out with the rest of its line, so no write ends mid-line
    // unless the wait ran out.
    if (fill) host_log_write_fd2(g_batch, fill);
    return done;
}

bool try_lock() {
    int z = 0;
    return g_drain_lock.compare_exchange_strong(z, 1, std::memory_order_acquire);
}
void unlock() { g_drain_lock.store(0, std::memory_order_release); }

// The writer's sleep, woken by a producer or after 10 ms.
void writer_wait(std::uint32_t seen) {
    timespec to{0, 10 * 1000 * 1000};
    syscall(SYS_futex, reinterpret_cast<std::uint32_t*>(&g_futex), FUTEX_WAIT_PRIVATE, seen, &to, nullptr, 0);
}

__attribute__((no_stack_protector)) bool push(const char* line, std::size_t len) {
    if (!len) return true;
    const std::uint64_t k = (len + kSlotBytes - 1) / kSlotBytes;
    if (k > 5) return false;  // host_log lines are under 1 KiB; anything longer is written directly
    std::uint64_t h = g_head.load(std::memory_order_relaxed);
    for (int spins = 0;;) {
        const std::uint64_t t = g_tail.load(std::memory_order_acquire);
        if (h + k - t > kSlots) {
            // Full: drain it here, as the synchronous write would have
            // stalled this thread anyway. While the writer holds the ring
            // (inside a write to a pipe nobody reads yet), wait for it: order
            // is kept, as the old write would have blocked too.
            if (try_lock()) {
                g_self_drains.fetch_add(1, std::memory_order_relaxed);
                drain_locked(0);
                unlock();
            } else if (++spins < 1000) {
                raw_yield();
            } else {
                if (spins == 1000) g_overflow.fetch_add(1, std::memory_order_relaxed);
                raw_sleep_us(200);
            }
            h = g_head.load(std::memory_order_relaxed);
            continue;
        }
        if (g_head.compare_exchange_weak(h, h + k, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            const std::uint64_t depth = h + k - t;
            if (depth > g_deepest.load(std::memory_order_relaxed)) g_deepest.store(depth, std::memory_order_relaxed);
            break;
        }
    }
    for (std::uint64_t i = 0; i < k; ++i) {
        Slot& s = g_ring[(h + i) & kMask];
        const std::size_t off = static_cast<std::size_t>(i) * kSlotBytes;
        const std::size_t n = len - off < kSlotBytes ? len - off : kSlotBytes;
        std::memcpy(s.data, line + off, n);
        s.len = static_cast<std::uint16_t>(n);
        s.last = i + 1 == k;
        s.seq.store(h + i + 1, std::memory_order_release);
    }
    g_lines.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);  // the stores above before the look at g_sleeping
    if (g_sleeping.load(std::memory_order_relaxed) && g_sleeping.exchange(0, std::memory_order_relaxed)) {
        g_futex.fetch_add(1, std::memory_order_relaxed);
        raw_syscall3(SYS_futex, reinterpret_cast<long>(&g_futex), FUTEX_WAKE_PRIVATE, 1);
    }
    return true;
}

void writer_main() {
    pthread_setname_np(pthread_self(), "bb-log");
    for (;;) {
        std::uint64_t n = 0;
        if (try_lock()) {
            n = drain_locked(now_ns() + 2000000);  // a producer mid-copy: wait up to 2 ms for it
            unlock();
        }
        if (n) continue;
        const std::uint32_t seen = g_futex.load(std::memory_order_relaxed);
        g_sleeping.store(1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const std::uint64_t t = g_tail.load(std::memory_order_relaxed);
        if (g_ring[t & kMask].seq.load(std::memory_order_acquire) == t + 1) {
            g_sleeping.store(0, std::memory_order_relaxed);
            continue;
        }
        writer_wait(seen);
        g_sleeping.store(0, std::memory_order_relaxed);
    }
}

}  // namespace

void host_log_start() {
    if (g_started.exchange(true)) return;
    const char* e = std::getenv("HELIAS_LOG_SYNC");
    if (e && *e == '1') {
        host_log("log: synchronous writes (HELIAS_LOG_SYNC=1)");
        return;
    }
    std::thread(writer_main).detach();
    std::atexit(host_log_sync_mode);  // later atexit handlers and destructors log synchronously
    g_host_log_sink = push;
}

__attribute__((no_stack_protector)) void host_log_flush() {
    if (g_head.load(std::memory_order_acquire) == g_tail.load(std::memory_order_acquire)) return;
    // Bounded: 500 ms for the lock (the writer may be inside a write), then
    // as long again for producers mid-copy.
    const std::uint64_t until = now_ns() + 500000000ull;
    while (!try_lock()) {
        if (now_ns() >= until) return;
        raw_yield();
    }
    drain_locked(now_ns() + 500000000ull);
    unlock();
}

void host_log_sync_mode() {
    g_host_log_sink = nullptr;
    host_log_flush();
}

void host_log_async_report() {
    if (!g_host_log_sink) return;
    host_log("log: %llu lines through the writer thread; deepest backlog %llu of %llu slots; %llu drained by a logging thread "
             "(ring full), %llu waited for the writer",
             static_cast<unsigned long long>(g_lines.load()), static_cast<unsigned long long>(g_deepest.load()),
             static_cast<unsigned long long>(kSlots), static_cast<unsigned long long>(g_self_drains.load()),
             static_cast<unsigned long long>(g_overflow.load()));
}

void host_log_async_line(char* out, std::size_t size) {
    std::snprintf(out, size,
                  "log_writer: on=%d lines=%llu deepest=%llu slots=%llu drained_by_loggers=%llu waited=%llu (rc6: the hot threads queue "
                  "their log lines; drained_by_loggers/waited above 0: a log storm the writer thread could not keep up with)",
                  g_host_log_sink ? 1 : 0, static_cast<unsigned long long>(g_lines.load()), static_cast<unsigned long long>(g_deepest.load()),
                  static_cast<unsigned long long>(kSlots), static_cast<unsigned long long>(g_self_drains.load()),
                  static_cast<unsigned long long>(g_overflow.load()));
}

#else

void host_log_async_line(char* out, std::size_t size) { std::snprintf(out, size, "log_writer: on=0"); }
void host_log_start() {}
void host_log_flush() {}
void host_log_sync_mode() {}
void host_log_async_report() {}

#endif
