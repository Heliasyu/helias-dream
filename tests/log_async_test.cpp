// rc6: core/log_async.cpp - the asynchronous log writer. Lines from many
// threads, short and long (several ring slots), through a pipe whose reader
// starts late so the ring fills and the producers drain it themselves; then
// every line must arrive exactly once, whole, and each thread's lines in the
// order it logged them, and nothing may be left after host_log_sync_mode().
#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
int main() {
    std::puts("log_async_test: Linux only");
    return 0;
}
#else
#include <fcntl.h>
#include <unistd.h>

int main() {
    int fds[2];
    if (pipe(fds) != 0) return 2;
    const int saved = dup(2);
    dup2(fds[1], 2);
    close(fds[1]);
    std::string got;
    std::atomic<bool> go{false};
    std::thread reader([&] {
        while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        char buf[65536];
        for (ssize_t n; (n = read(fds[0], buf, sizeof(buf))) > 0;) got.append(buf, static_cast<std::size_t>(n));
    });
    host_log("before the writer: written directly");
    host_log_start();
    constexpr int kThreads = 8, kLines = 20000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([t] {
            std::string pad;
            for (int i = 0; i < kLines; ++i) {
                const int len = (i * 7919 + t * 104729) % 900;  // 0..899 bytes of padding: 1 to 4 slots
                pad.assign(static_cast<std::size_t>(len), static_cast<char>('a' + (i + t) % 26));
                host_log("t%d n%d [%s] end", t, i, pad.c_str());
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // the reader starts late: the ring fills
    go.store(true);
    for (auto& th : ts) th.join();
    host_log("after the threads");
    host_log_sync_mode();
    host_log("after sync mode: written directly");
    host_log_async_report();
    fflush(stderr);
    dup2(saved, 2);  // closes the pipe's last write end: the reader sees EOF
    reader.join();

    int bad = 0;
    std::map<int, int> next;
    std::size_t lines = 0, pos = 0;
    bool first = true, saw_after = false, saw_sync = false;
    while (pos < got.size()) {
        const std::size_t nl = got.find('\n', pos);
        if (nl == std::string::npos) {
            std::fprintf(stderr, "log_async_test: a line without its newline at the end\n");
            ++bad;
            break;
        }
        const std::string l = got.substr(pos, nl - pos);
        pos = nl + 1;
        ++lines;
        if (first) {
            first = false;
            if (l != "[heliasdream] before the writer: written directly") ++bad;
            continue;
        }
        if (l == "[heliasdream] after the threads") {
            saw_after = true;
            continue;
        }
        if (l == "[heliasdream] after sync mode: written directly") {
            if (!saw_after) ++bad;
            saw_sync = true;
            continue;
        }
        if (l.rfind("[heliasdream] log: ", 0) == 0) continue;
        int t = -1, i = -1;
        if (std::sscanf(l.c_str(), "[heliasdream] t%d n%d [", &t, &i) != 2 || t < 0 || t >= kThreads) {
            if (bad++ < 5) std::fprintf(stderr, "log_async_test: a mangled line: %.80s\n", l.c_str());
            continue;
        }
        if (saw_after) ++bad;  // a thread's line after the line logged after the joins
        const int len = (i * 7919 + t * 104729) % 900;
        const std::string pad(static_cast<std::size_t>(len), static_cast<char>('a' + (i + t) % 26));
        char want[1024];
        std::snprintf(want, sizeof(want), "[heliasdream] t%d n%d [%s] end", t, i, pad.c_str());
        if (l != want) {
            if (bad++ < 5) std::fprintf(stderr, "log_async_test: line t%d n%d differs\n", t, i);
        }
        if (next[t] != i) {
            if (bad++ < 5) std::fprintf(stderr, "log_async_test: t%d: n%d where n%d was next\n", t, i, next[t]);
        }
        next[t] = i + 1;
    }
    for (int t = 0; t < kThreads; ++t) {
        if (next[t] != kLines) {
            std::fprintf(stderr, "log_async_test: t%d: %d of %d lines\n", t, next[t], kLines);
            ++bad;
        }
    }
    if (!saw_after || !saw_sync) ++bad;
    std::printf("log_async_test: %zu lines, %zu bytes, %d problems\n", lines, got.size(), bad);
    return bad ? 1 : 0;
}
#endif
