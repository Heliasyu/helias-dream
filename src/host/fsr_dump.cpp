// heliasdream-vk test3: the FSR stage dump (host/fsr_dump.h).
#include "host/fsr_dump.h"

#include "core/image_file.h"
#include "host/gpu_internal.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> g_requested{false};
std::atomic<int> g_writing{0};
}  // namespace

void host_fsr_dump_request(const char* why) {
    if (!gpu::fsr_dump_enabled()) return;
    if (!g_requested.exchange(true)) host_log("fsr-dump: the next upscaled frame (%s)", why ? why : "asked");
}

namespace gpu {
namespace {

// frame_capture's folder and switch (host/frame_capture.h), read the same
// way here: that file is only built with the window.
std::string capture_folder() {
    if (const char* e = std::getenv("HELIAS_VK_CAPTURE_DIR"); e && e[0]) return e;
    std::error_code ec;
    return std::filesystem::is_directory("logs", ec) ? "logs/captures" : "captures";
}
bool presented_capture_on() {
    const char* e = std::getenv("HELIAS_VK_CAPTURE");
    return e && e[0] == '1';
}

struct Staged {
    std::string name;
    DevBuffer buf;
    std::uint32_t w = 0, h = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    temporal::DumpKind kind = temporal::DumpKind::Color;
};
std::vector<Staged> g_staged;
std::vector<std::string> g_notes;
bool g_active = false;

std::string folder_for(std::uint64_t flip) {
    const std::string base = capture_folder();
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    std::string dir = base + "/fsr-" + stamp + "-flip" + std::to_string(flip);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool readable(VkFormat f, temporal::DumpKind k) {
    switch (k) {
    case temporal::DumpKind::Color:
    case temporal::DumpKind::Motion: return f == VK_FORMAT_R16G16B16A16_SFLOAT;
    case temporal::DumpKind::Depth: return f == VK_FORMAT_R32_SFLOAT;
    case temporal::DumpKind::Mask: return f == VK_FORMAT_R8_UNORM;
    }
    return false;
}

void write_one(const std::string& dir, const Staged& s, std::string* info) {
    const std::size_t pitch = static_cast<std::size_t>(s.w) * temporal::dump_bpp(s.kind);
    const auto* px = static_cast<const std::uint8_t*>(s.buf.map);
    const temporal::DumpStats st = temporal::dump_stats(s.kind, px, s.w, s.h, pitch);
    char line[512];
    std::snprintf(line, sizeof(line),
                  "%s: %ux%u format %d (%s): min %.4g mean %.4g max %.4g; mean x < 3/4 %.4g, x >= 3/4 %.4g; NaN %llu, infinite %llu",
                  s.name.c_str(), s.w, s.h, static_cast<int>(s.format), temporal::dump_kind_name(s.kind), st.min, st.mean, st.max, st.mean_left,
                  st.mean_right, static_cast<unsigned long long>(st.nan), static_cast<unsigned long long>(st.inf));
    *info += line;
    *info += "\n";
    host_log("fsr-dump: %s", line);
    ImageFile f;
    const std::string path = dir + "/" + s.name + ".png";
    if (!f.open(path, s.w, s.h)) {
        host_log("fsr-dump: cannot write %s", path.c_str());
        return;
    }
    std::vector<std::uint8_t> row(static_cast<std::size_t>(s.w) * 3);
    for (std::uint32_t y = 0; y < s.h; ++y) {
        temporal::dump_row_rgb8(s.kind, px + y * pitch, s.w, st.min, st.max, row.data());
        f.row(row.data());
    }
    if (!f.close()) host_log("fsr-dump: writing %s failed", path.c_str());
}

}  // namespace

bool fsr_dump_enabled() {
    static const bool on = [] {
        const char* e = std::getenv("HELIAS_FSR_DUMP");
        return e && e[0] == '1';
    }();
    return on;
}

bool fsr_dump_wanted_locked(std::uint64_t flip, std::uint64_t upscaled) {
    if (!fsr_dump_enabled() || g_writing.load() > 0) return false;
    static bool said = false;
    if (!said) {
        said = true;
        host_log("fsr-dump: HELIAS_FSR_DUMP=1: fsr-dump.request or capture.request in %s (bbtest mark), or Shift+F12 with "
                 "HELIAS_VK_CAPTURE=1, dumps the next upscaled frame's FSR inputs, output and target; once by itself at the 600th",
                 capture_folder().c_str());
    }
    if (flip % 30 == 0) {
        std::error_code ec;
        const std::filesystem::path dir(capture_folder());
        if (std::filesystem::exists(dir / "fsr-dump.request", ec)) {
            std::filesystem::remove(dir / "fsr-dump.request", ec);
            host_fsr_dump_request("fsr-dump.request");
        }
        // With the presented-frame capture on, its own poll takes the file
        // (and asks here through host_fsr_dump_request); without, here.
        if (!presented_capture_on() && std::filesystem::exists(dir / "capture.request", ec)) {
            std::filesystem::remove(dir / "capture.request", ec);
            host_fsr_dump_request("capture.request");
        }
    }
    static const bool auto_on = [] {
        const char* e = std::getenv("HELIAS_FSR_DUMP_AUTO");
        return !(e && e[0] == '0');
    }();
    if (auto_on && upscaled == 600) host_fsr_dump_request("the 600th upscaled frame");
    if (!g_requested.exchange(false)) return false;
    g_staged.clear();
    g_notes.clear();
    g_active = true;
    return true;
}

bool fsr_dump_add_locked(const char* name, VkImage image, VkFormat format, std::uint32_t w, std::uint32_t h, temporal::DumpKind kind) {
    if (!g_active) return false;
    if (!image || !w || !h) {
        g_notes.push_back(std::string(name) + ": none this frame");
        return false;
    }
    if (!readable(format, kind)) {
        g_notes.push_back(std::string(name) + ": format " + std::to_string(static_cast<int>(format)) + " is not one the dump reads");
        return false;
    }
    Staged s;
    s.name = name;
    s.w = w;
    s.h = h;
    s.format = format;
    s.kind = kind;
    if (!create_dev_buffer(s.buf, static_cast<std::uint64_t>(w) * h * temporal::dump_bpp(kind), true, true)) {
        g_notes.push_back(std::string(name) + ": no staging memory");
        return false;
    }
    begin_recording_locked();
    render_end_pass_locked();
    VkCommandBuffer cmd = g_cmd();
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_GENERAL, s.buf.buffer, 1, &region);
    VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &hb, 0,
                         nullptr, 0, nullptr);
    g_staged.push_back(std::move(s));
    return true;
}

void fsr_dump_note_locked(const std::string& line) {
    if (g_active) g_notes.push_back(line);
}

void fsr_dump_finish_locked(std::uint64_t flip) {
    if (!g_active) return;
    g_active = false;
    const auto t0 = std::chrono::steady_clock::now();
    flush_locked();  // submits and waits: the copies have landed
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const std::string dir = folder_for(flip);
    auto staged = std::make_shared<std::vector<Staged>>(std::move(g_staged));
    g_staged.clear();
    std::string notes;
    for (const std::string& n : g_notes) notes += n + "\n";
    g_notes.clear();
    host_log("fsr-dump: flip %llu: %zu images into %s (the GPU wait %.1f ms)", static_cast<unsigned long long>(flip), staged->size(), dir.c_str(),
             ms);
    g_writing.fetch_add(1);
    VkDevice device = g.device;
    std::thread([dir, staged, notes, device] {
        std::string info = notes;
        for (const Staged& s : *staged) write_one(dir, s, &info);
        std::ofstream(dir + "/info.txt") << info;
        for (Staged& s : *staged) {
            if (s.buf.map) vkUnmapMemory(device, s.buf.memory);
            vkDestroyBuffer(device, s.buf.buffer, nullptr);
            vkFreeMemory(device, s.buf.memory, nullptr);
        }
        host_log("fsr-dump: written to %s", dir.c_str());
        g_writing.fetch_sub(1);
    }).detach();
}

}  // namespace gpu
