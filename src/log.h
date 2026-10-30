#pragma once

#include <cstdio>
#include <cstring>
#include <utility>
#if !defined(_WIN32)
#include <unistd.h>
#endif

// heliasdream-vk rc2: a tap on every line (host/perf_capture.cpp keeps the last
// 30 s for its captures). Lock-free on the tap's side: the fault handler logs.
inline void (*volatile g_host_log_tap)(const char* line, std::size_t len) = nullptr;

// rc6: the asynchronous sink (core/log_async.cpp). Null until
// host_log_start(), and again in crash mode or with HELIAS_LOG_SYNC=1: then
// every line is written by its own thread, as before. The sink returns false
// for a line it did not take (the caller writes it itself).
inline bool (*volatile g_host_log_sink)(const char* line, std::size_t len) = nullptr;

// Starts the writer thread (Linux; a no-op elsewhere). Lines logged before it
// are written synchronously.
void host_log_start();
// Writes every line queued so far; bounded (a stuck producer cannot hold the
// caller forever). Safe from a signal handler on the host's FS.
void host_log_flush();
// From here on every line is written synchronously, after what was queued
// (the crash and exit paths).
void host_log_sync_mode();
// One line of the writer's counters (the exit reports).
void host_log_async_report();
// The snapshot's [cpu] `log_writer:` line (no newline).
void host_log_async_line(char* out, std::size_t size);

#if !defined(_WIN32)
// One write(2) of the line's remaining bytes, or the whole of them handed to
// the asynchronous sink (then all of them count as written). Out of line on
// purpose: host_log_line, inlined into every caller, keeps the shape it had
// before rc6 (the same loop around one call), so the PGO profile of the
// functions that log still matches their code.
__attribute__((noinline)) inline ssize_t host_log_write2(const char* p, std::size_t left) {
    if (auto* sink = g_host_log_sink) {
        if (sink(p, left)) return static_cast<ssize_t>(left);
    }
    return ::write(2, p, left);
}
// write(2), not stdio: async-signal-safe (the fault handler logs) and no
// FILE lock to deadlock on when a fault lands inside fputs.
inline void host_log_write_fd2(const char* p, std::size_t left) {
    while (left) {
        const ssize_t n = ::write(2, p, left);
        if (n <= 0) {
            break;
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
}
#endif

// One write per line so lines from different threads do not interleave.
inline void host_log_line(const char* line) {
    if (auto* tap = g_host_log_tap) tap(line, std::strlen(line));
#if defined(_WIN32)
    std::fputs(line, stderr);
#else
    const char* p = line;
    std::size_t left = std::strlen(line);
    while (left) {
        const ssize_t n = host_log_write2(p, left);
        if (n <= 0) {
            break;
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
#endif
}

template <typename... Args>
inline void host_log(const char* fmt, Args&&... args) {
    char buf[1024];
    int n = std::snprintf(buf, sizeof(buf) - 1, "[heliasdream] ");
    if (n < 0) {
        return;
    }
    int m;
    if constexpr (sizeof...(Args) == 0) {
        m = std::snprintf(buf + n, sizeof(buf) - 1 - static_cast<std::size_t>(n), "%s", fmt);
    } else {
        m = std::snprintf(buf + n, sizeof(buf) - 1 - static_cast<std::size_t>(n), fmt,
                          std::forward<Args>(args)...);
    }
    if (m < 0) {
        return;
    }
    std::size_t len = static_cast<std::size_t>(n) + static_cast<std::size_t>(m);
    if (len > sizeof(buf) - 2) {
        len = sizeof(buf) - 2;
    }
    buf[len] = '\n';
    buf[len + 1] = 0;
    host_log_line(buf);
}
