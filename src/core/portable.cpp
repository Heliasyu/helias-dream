#include "core/portable.h"
#include "log.h"

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#include <ctime>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#include <malloc.h>
#include <windows.h>
#include <timeapi.h>
#else
#include <dirent.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

bool host_mkdir(const char* path) {
#if defined(_WIN32)
    return _mkdir(path) == 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}

bool host_list_dir(const char* path, std::vector<std::string>* names) {
    names->clear();
#if defined(_WIN32)
    std::string pattern = std::string(path) + "\\*";
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (std::strcmp(fd.cFileName, ".") != 0 && std::strcmp(fd.cFileName, "..") != 0) names->push_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return true;
#else
    DIR* d = ::opendir(path);
    if (!d) return false;
    while (dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) names->push_back(e->d_name);
    }
    ::closedir(d);
    return true;
#endif
}

std::uint64_t host_process_cpu_ms() {
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    const auto to64 = [](const FILETIME& f) {
        return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return (to64(k) + to64(u)) / 10000;  // 100 ns units
#else
    struct rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    return static_cast<std::uint64_t>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000 +
           static_cast<std::uint64_t>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000;
#endif
}

std::uint64_t host_thread_cpu_ns() {
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    const auto to64 = [](const FILETIME& f) {
        return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return (to64(k) + to64(u)) * 100;  // 100 ns units
#else
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(ts.tv_nsec);
#endif
}

#if !defined(_WIN32)
namespace {
// Set while this thread copies in host_read_safe: a fault in the copy comes
// back there (host_read_safe_recover, from the SIGSEGV handler).
thread_local sigjmp_buf* t_safe_read = nullptr;
}  // namespace

void host_read_safe_recover() {
    if (sigjmp_buf* jb = t_safe_read) {
        t_safe_read = nullptr;
        siglongjmp(*jb, 1);
    }
}
#endif

bool host_read_safe(const void* addr, void* out, std::size_t len) {
#if defined(_WIN32)
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), addr, out, len, &got) && got == len;
#else
    // A plain copy under a jump buffer: a page that is not mapped faults, and
    // the SIGSEGV handler brings the thread back here. It was
    // process_vm_readv(getpid(), ...) - two system calls a read, and the GX
    // resource registry reads a holder's fields one at a time: ~0.6 ms of the
    // main loop in a frame that creates many resources.
    sigjmp_buf jb;
    if (sigsetjmp(jb, 0) != 0) {
        // The handler ran with its signal blocked, and a jump out of it that
        // saved no mask leaves it so: the next fault would kill the process.
        sigset_t faults;
        sigemptyset(&faults);
        sigaddset(&faults, SIGSEGV);
        sigaddset(&faults, SIGBUS);
        pthread_sigmask(SIG_UNBLOCK, &faults, nullptr);
        return false;
    }
    t_safe_read = &jb;
    std::atomic_signal_fence(std::memory_order_seq_cst);  // the copy stays between the two stores
    std::memcpy(out, addr, len);
    std::atomic_signal_fence(std::memory_order_seq_cst);
    t_safe_read = nullptr;
    return true;
#endif
}

bool host_thread_stack(std::uint64_t* lo, std::uint64_t* hi) {
    // Asked once a thread: the bounds do not move, and pthread_getattr_np
    // reads /proc/self/maps for the main thread.
    static thread_local std::uint64_t l = 0, h = 0;
    if (h == 0) {
        h = 1;  // asked and refused, unless the call below says otherwise
#if defined(_WIN32)
        ULONG_PTR low = 0, high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        if (low && high > low) {
            l = static_cast<std::uint64_t>(low);
            h = static_cast<std::uint64_t>(high);
        }
#else
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) == 0) {
            void* addr = nullptr;
            std::size_t size = 0;
            if (pthread_attr_getstack(&attr, &addr, &size) == 0 && addr && size) {
                l = reinterpret_cast<std::uint64_t>(addr);
                h = l + size;
            }
            pthread_attr_destroy(&attr);
        }
#endif
    }
    if (h <= 1) return false;
    *lo = l;
    *hi = h;
    return true;
}

std::uint32_t host_thread_id() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentThreadId());
#else
    return static_cast<std::uint32_t>(syscall(SYS_gettid));
#endif
}

#if defined(_WIN32)
namespace {
struct NamedThread {
    DWORD tid;
    HANDLE h;
    char name[32];
};
std::mutex g_named_mu;
std::vector<NamedThread> g_named;  // under g_named_mu

void register_named_thread(const char* name) {
    const DWORD tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lk(g_named_mu);
    for (NamedThread& t : g_named) {
        if (t.tid == tid) {
            std::snprintf(t.name, sizeof(t.name), "%s", name);
            return;
        }
    }
    HANDLE h = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0)) return;
    NamedThread t{tid, h, {}};
    std::snprintf(t.name, sizeof(t.name), "%s", name);
    g_named.push_back(t);
}
}  // namespace
#endif

void host_thread_fit_name(const char* name, char (&out)[16]) {
    // Linux keeps 15 characters and refuses a longer name outright, so the
    // game's "Core.Res.PostProcessor_0" stayed "heliasdream" in /proc, the frame
    // statistics and the sampler. Leading dotted parts go first
    // ("PostProcessor_0"), then the tail.
    const char* fit = name ? name : "";
    while (std::strlen(fit) > 15) {
        const char* dot = std::strchr(fit, '.');
        if (!dot || !dot[1]) break;
        fit = dot + 1;
    }
    std::snprintf(out, sizeof(out), "%s", fit);
}

std::vector<HostThreadTime> host_thread_times() {
    std::vector<HostThreadTime> out;
#if defined(_WIN32)
    std::lock_guard<std::mutex> lk(g_named_mu);
    out.reserve(g_named.size());
    for (std::size_t i = 0; i < g_named.size();) {
        NamedThread& t = g_named[i];
        DWORD code = 0;
        FILETIME created, exited, kernel, user;
        if (!GetExitCodeThread(t.h, &code) || code != STILL_ACTIVE || !GetThreadTimes(t.h, &created, &exited, &kernel, &user)) {
            CloseHandle(t.h);
            g_named[i] = g_named.back();
            g_named.pop_back();
            continue;
        }
        const auto ft = [](const FILETIME& f) { return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
        HostThreadTime r{static_cast<std::uint32_t>(t.tid), ft(kernel) + ft(user), {}};
        std::memcpy(r.name, t.name, sizeof(r.name));
        out.push_back(r);
        ++i;
    }
#endif
    return out;
}

void host_thread_set_name(const char* name) {
    if (!name || !name[0]) return;
#if defined(_WIN32)
    pthread_setname_np(pthread_self(), name);
#else
    char comm[16];
    host_thread_fit_name(name, comm);
    pthread_setname_np(pthread_self(), comm);
#endif
#if defined(_WIN32)
    register_named_thread(name);
    using SetDesc = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const SetDesc set_desc = [] {
        HMODULE k = GetModuleHandleW(L"kernelbase.dll");
        if (!k) k = GetModuleHandleW(L"kernel32.dll");
        return k ? reinterpret_cast<SetDesc>(GetProcAddress(k, "SetThreadDescription")) : nullptr;
    }();
    if (!set_desc) {
        static bool said = false;
        if (!said) {
            said = true;
            host_log("thread names: SetThreadDescription not available; threads are unnamed to the sampler");
        }
        return;
    }
    wchar_t w[64];
    int i = 0;
    for (; i < 63 && name[i]; ++i) w[i] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
    w[i] = 0;
    set_desc(GetCurrentThread(), w);
#endif
}

bool host_desktop_max_size(int* w, int* h) {
#if defined(_WIN32)
    // EnumDisplaySettings gives each display's mode in physical pixels,
    // whatever the process's DPI awareness (the monitor rectangles would be
    // scaled for a process that is not aware).
    int bw = 0, bh = 0;
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            DEVMODEW dm{};
            dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
                bw = std::max(bw, static_cast<int>(dm.dmPelsWidth));
                bh = std::max(bh, static_cast<int>(dm.dmPelsHeight));
            }
        }
        dd = DISPLAY_DEVICEW{};
        dd.cb = sizeof(dd);
    }
    if (bw <= 0 || bh <= 0) return false;
    *w = bw;
    *h = bh;
    return true;
#else
    // heliasdream-vk: each connected, enabled DRM connector's modes in sysfs
    // (/sys/class/drm/card<N>-<connector>/modes, "<w>x<h>" a line, physical
    // pixels whatever the desktop's scaling), the largest of them. Before the
    // window exists, so not SDL's. Without it a Linux start fell back to the
    // Resolution setting's largest entries (5120x2160), and a start at Native
    // AA on a 4K screen could not split the scene live (the display buffers
    // must be the output's size).
    int bw = 0, bh = 0;
    if (DIR* d = opendir("/sys/class/drm")) {
        while (const dirent* e = readdir(d)) {
            const std::string base = std::string("/sys/class/drm/") + e->d_name;
            if (std::strncmp(e->d_name, "card", 4) != 0 || !std::strchr(e->d_name, '-')) continue;  // connectors only
            const auto first_line = [](const std::string& path) {
                char line[64] = {};
                if (FILE* f = std::fopen(path.c_str(), "r")) {
                    if (!std::fgets(line, sizeof(line), f)) line[0] = 0;
                    std::fclose(f);
                }
                line[std::strcspn(line, "\n")] = 0;
                return std::string(line);
            };
            if (first_line(base + "/status") != "connected") continue;
            if (const std::string en = first_line(base + "/enabled"); !en.empty() && en != "enabled") continue;
            if (FILE* f = std::fopen((base + "/modes").c_str(), "r")) {
                char line[64];
                while (std::fgets(line, sizeof(line), f)) {
                    int mw = 0, mh = 0;
                    if (std::sscanf(line, "%dx%d", &mw, &mh) == 2 && mw > 0 && mh > 0) {
                        bw = std::max(bw, mw);
                        bh = std::max(bh, mh);
                    }
                }
                std::fclose(f);
            }
        }
        closedir(d);
    }
    if (bw <= 0 || bh <= 0) return false;
    *w = bw;
    *h = bh;
    return true;
#endif
}

void host_sleep_us(std::uint64_t usec) {
#if defined(_WIN32)
    static thread_local HANDLE timer = [] {
        static const bool period = [] {
            timeBeginPeriod(1);  // for Sleep and the waits that fall back to the tick
            return true;
        }();
        (void)period;
        HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!h) h = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        return h;
    }();
    if (usec == 0) {
        Sleep(0);
        return;
    }
    if (timer) {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(usec * 10);  // 100 ns units, relative
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }
    Sleep(static_cast<DWORD>((usec + 999) / 1000));
#else
    usleep(static_cast<useconds_t>(usec));
#endif
}

void host_sleep_until(std::chrono::steady_clock::time_point t) {
    const auto now = std::chrono::steady_clock::now();
    if (t <= now) return;
    host_sleep_us(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t - now).count()));
}

// rc6: 32 positions at a time (AVX2 where the CPU has it, else 16 with SSE2,
// every x86-64): a candidate is a position whose first and last bytes both
// match the needle's, and only those are compared in full - the camera's table
// search (engine/camera.cpp) went through 8 GiB of guest memory a byte at a
// time. The first match, as the byte loop (still the fallback and the tail)
// finds it.
#if defined(__x86_64__)
namespace {
__attribute__((target("avx2"))) std::size_t memmem_avx2(const unsigned char* h, std::size_t hay_len, const unsigned char* n,
                                                         std::size_t needle_len, const unsigned char** hit) {
    const __m256i first = _mm256_set1_epi8(static_cast<char>(n[0]));
    const __m256i last = _mm256_set1_epi8(static_cast<char>(n[needle_len - 1]));
    std::size_t i = 0;
    for (; i + 32 + needle_len - 1 <= hay_len; i += 32) {
        const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(h + i));
        const __m256i z = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(h + i + needle_len - 1));
        unsigned mask = static_cast<unsigned>(_mm256_movemask_epi8(_mm256_and_si256(_mm256_cmpeq_epi8(a, first), _mm256_cmpeq_epi8(z, last))));
        while (mask) {
            const unsigned bit = static_cast<unsigned>(__builtin_ctz(mask));
            if (std::memcmp(h + i + bit + 1, n + 1, needle_len - 2) == 0) {
                *hit = h + i + bit;
                return i;
            }
            mask &= mask - 1;
        }
    }
    return i;
}
const bool g_have_avx2 = __builtin_cpu_supports("avx2");
}  // namespace
#endif

const void* host_memmem(const void* hay, std::size_t hay_len, const void* needle, std::size_t needle_len) {
    if (!needle_len) return hay;
    if (!hay || hay_len < needle_len) return nullptr;
    const auto* h = static_cast<const unsigned char*>(hay);
    const auto* n = static_cast<const unsigned char*>(needle);
    std::size_t i = 0;
#if defined(__SSE2__)
    if (needle_len >= 2) {
#if defined(__x86_64__)
        if (g_have_avx2) {
            const unsigned char* hit = nullptr;
            i = memmem_avx2(h, hay_len, n, needle_len, &hit);
            if (hit) return hit;
        }
#endif
        const __m128i first = _mm_set1_epi8(static_cast<char>(n[0]));
        const __m128i last = _mm_set1_epi8(static_cast<char>(n[needle_len - 1]));
        for (; i + 16 + needle_len - 1 <= hay_len; i += 16) {
            const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(h + i));
            const __m128i z = _mm_loadu_si128(reinterpret_cast<const __m128i*>(h + i + needle_len - 1));
            unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(_mm_and_si128(_mm_cmpeq_epi8(a, first), _mm_cmpeq_epi8(z, last))));
            while (mask) {
                const unsigned bit = static_cast<unsigned>(__builtin_ctz(mask));
                if (std::memcmp(h + i + bit + 1, n + 1, needle_len - 2) == 0) return h + i + bit;
                mask &= mask - 1;
            }
        }
    }
#endif
    for (; i + needle_len <= hay_len; ++i) {
        if (h[i] == n[0] && std::memcmp(h + i, n, needle_len) == 0) return h + i;
    }
    return nullptr;
}

void* host_guest_malloc(std::size_t n) {
#if defined(_WIN32)
    return _aligned_malloc(n ? n : 1, 16);
#else
    return std::malloc(n);
#endif
}

void* host_guest_calloc(std::size_t n, std::size_t size) {
#if defined(_WIN32)
    const std::size_t total = n * size;
    void* p = _aligned_malloc(total ? total : 1, 16);
    if (p) std::memset(p, 0, total);
    return p;
#else
    return std::calloc(n, size);
#endif
}

void* host_guest_realloc(void* p, std::size_t n) {
#if defined(_WIN32)
    return _aligned_realloc(p, n ? n : 1, 16);
#else
    return std::realloc(p, n);
#endif
}

void* host_guest_memalign(std::size_t align, std::size_t n) {
    const std::size_t size = n ? n : 1;
#if defined(_WIN32)
    void* p = _aligned_malloc(size, align < 16 ? 16 : align);
#else
    void* p = nullptr;
    if (posix_memalign(&p, align < 16 ? 16 : align, size) != 0) return nullptr;
#endif
    if (p) std::memset(p, 0, size);
    return p;
}

void host_guest_free(void* p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void* host_page_alloc(std::size_t size, bool exec) {
#if defined(_WIN32)
    void* p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, exec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
    return p;
#else
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE | (exec ? PROT_EXEC : 0), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
#endif
}

void host_page_free(void* p, std::size_t size) {
    if (!p) return;
#if defined(_WIN32)
    (void)size;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, size);
#endif
}

void host_page_guard(void* p, std::size_t size) {
#if defined(_WIN32)
    DWORD old = 0;
    VirtualProtect(p, size, PAGE_NOACCESS, &old);
#else
    mprotect(p, size, PROT_NONE);
#endif
}

#if defined(_WIN32)
extern "C" IMAGE_DOS_HEADER __ImageBase;
std::uintptr_t host_image_base() { return reinterpret_cast<std::uintptr_t>(&__ImageBase); }
#else
extern "C" char __executable_start;
std::uintptr_t host_image_base() { return reinterpret_cast<std::uintptr_t>(&__executable_start); }
#endif
