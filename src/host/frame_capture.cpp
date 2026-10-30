// heliasdream-vk's presented-frame capture (host/frame_capture.h).
#include "host/frame_capture.h"

#include "core/image_file.h"
#include "host/fsr_dump.h"
#include "host/settings.h"
#include "host/upscaler_presets.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace frame_capture {
namespace {

std::atomic<bool> g_wanted{false};
std::atomic<bool> g_usage_ok{false};
std::atomic<int> g_writing{0};  // files still being written (bounded)

struct Staging {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* map = nullptr;
    VkDeviceSize size = 0;
};
Staging g_staging;          // presenter thread only
bool g_in_flight = false;   // a copy recorded, its fence not yet waited
VkFormat g_format = VK_FORMAT_UNDEFINED;
VkExtent2D g_extent{};
std::string g_label;  // the upscaler setting when it was taken

std::string capture_dir() {
    if (const char* e = std::getenv("HELIAS_VK_CAPTURE_DIR"); e && e[0]) return e;
    std::error_code ec;
    return std::filesystem::is_directory("logs", ec) ? "logs/captures" : "captures";
}

double every_seconds() {
    static const double s = [] {
        const char* e = std::getenv("HELIAS_VK_CAPTURE_EVERY");
        return e && e[0] ? std::atof(e) : 0.0;
    }();
    return s;
}

void destroy_staging(VkDevice device) {
    if (g_staging.map) vkUnmapMemory(device, g_staging.memory);
    if (g_staging.buffer) vkDestroyBuffer(device, g_staging.buffer, nullptr);
    if (g_staging.memory) vkFreeMemory(device, g_staging.memory, nullptr);
    g_staging = Staging{};
}

bool make_staging(VkDevice device, VkPhysicalDevice phys, VkDeviceSize size) {
    if (g_staging.buffer && g_staging.size >= size) return true;
    destroy_staging(device);
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &g_staging.buffer) != VK_SUCCESS) {
        g_staging.buffer = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, g_staging.buffer, &req);
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    std::uint32_t type = UINT32_MAX;
    // Host-cached first: the PNG writer reads every byte back.
    const VkMemoryPropertyFlags wants[2] = {
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    for (const VkMemoryPropertyFlags want : wants) {
        for (std::uint32_t i = 0; i < mp.memoryTypeCount && type == UINT32_MAX; ++i)
            if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) type = i;
        if (type != UINT32_MAX) break;
    }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (type == UINT32_MAX || vkAllocateMemory(device, &mai, nullptr, &g_staging.memory) != VK_SUCCESS) {
        g_staging.memory = VK_NULL_HANDLE;
        destroy_staging(device);
        return false;
    }
    vkBindBufferMemory(device, g_staging.buffer, g_staging.memory, 0);
    if (vkMapMemory(device, g_staging.memory, 0, VK_WHOLE_SIZE, 0, &g_staging.map) != VK_SUCCESS) {
        g_staging.map = nullptr;
        destroy_staging(device);
        return false;
    }
    g_staging.size = size;
    return true;
}

bool format_ok(VkFormat f) {
    return f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_R8G8B8A8_UNORM || f == VK_FORMAT_R8G8B8A8_SRGB ||
           f == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || f == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
}

std::string file_name(const std::string& label) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[40];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    char ms_s[8];
    std::snprintf(ms_s, sizeof(ms_s), "-%03d", ms);
    std::string tag;
    for (char c : label) tag += (c == ' ' || c == '.') ? '_' : c;
    return std::string("frame-") + stamp + ms_s + "-" + tag + ".png";
}

void write_png(std::vector<std::uint8_t> px, VkFormat format, VkExtent2D extent, std::string label) {
    const std::string dir = capture_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/" + file_name(label);
    ImageFile f;
    if (!f.open(path, extent.width, extent.height)) {
        host_log("capture: cannot write %s", path.c_str());
        return;
    }
    const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
    const bool ten = format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    const bool ten_bgr = format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;  // R in the high bits
    std::vector<std::uint8_t> row(static_cast<std::size_t>(extent.width) * 3);
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        const std::uint8_t* src = px.data() + static_cast<std::size_t>(y) * extent.width * 4;
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const std::uint8_t* p = src + x * 4;
            if (ten) {
                std::uint32_t v;
                std::memcpy(&v, p, 4);
                const std::uint32_t lo = v & 0x3ff, mid = (v >> 10) & 0x3ff, hi = (v >> 20) & 0x3ff;
                const std::uint32_t r = ten_bgr ? hi : lo, b = ten_bgr ? lo : hi;
                row[x * 3 + 0] = static_cast<std::uint8_t>(r >> 2);
                row[x * 3 + 1] = static_cast<std::uint8_t>(mid >> 2);
                row[x * 3 + 2] = static_cast<std::uint8_t>(b >> 2);
            } else {
                row[x * 3 + 0] = bgra ? p[2] : p[0];
                row[x * 3 + 1] = p[1];
                row[x * 3 + 2] = bgra ? p[0] : p[2];
            }
        }
        f.row(row.data());
    }
    if (!f.close()) {
        host_log("capture: writing %s failed", path.c_str());
        return;
    }
    std::error_code aec;
    const std::filesystem::path abs = std::filesystem::absolute(path, aec);
    host_log("capture: wrote %s (%ux%u, the presented frame, upscaler setting %s)", aec ? path.c_str() : abs.string().c_str(),
             extent.width, extent.height, label.c_str());
}

}  // namespace

bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("HELIAS_VK_CAPTURE");
        return e && e[0] == '1';
    }();
    return on;
}

VkImageUsageFlags swapchain_usage(VkImageUsageFlags supported) {
    if (!enabled()) return 0;
    const bool ok = (supported & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    static bool said = false;
    if (!said) {
        said = true;
        host_log("capture: HELIAS_VK_CAPTURE=1: %s; folder %s", ok ? "Shift+F12 or capture.request captures the presented frame"
                                                                   : "the surface takes no transfer reads, so frames cannot be captured",
                 capture_dir().c_str());
    }
    g_usage_ok.store(ok);
    return ok ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0;
}

void request(const char* why) {
    host_fsr_dump_request(why);
    if (!enabled()) return;
    if (!g_usage_ok.load()) {
        host_log("capture: %s asked for a frame, but the swapchain cannot be read", why ? why : "?");
        return;
    }
    if (!g_wanted.exchange(true)) host_log("capture: the next presented frame (%s)", why ? why : "asked");
}

void poll() {
    if (!enabled()) return;
    static auto last_check = std::chrono::steady_clock::now();
    static auto last_shot = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - last_check >= std::chrono::milliseconds(100)) {
        last_check = now;
        std::error_code ec;
        const std::filesystem::path req = std::filesystem::path(capture_dir()) / "capture.request";
        if (std::filesystem::exists(req, ec)) {
            std::filesystem::remove(req, ec);
            request("capture.request");
        }
    }
    const double every = every_seconds();
    if (every > 0.0 && std::chrono::duration<double>(now - last_shot).count() >= every) {
        last_shot = now;
        request("HELIAS_VK_CAPTURE_EVERY");
    }
}

bool record(VkCommandBuffer cmd, VkDevice device, VkPhysicalDevice phys, VkImage image, VkFormat format, VkExtent2D extent) {
    if (!enabled() || !g_usage_ok.load() || g_in_flight || !g_wanted.load()) return false;
    if (g_writing.load() >= 3) return false;  // the disk is behind: the next frame
    g_wanted.store(false);
    if (!format_ok(format)) {
        host_log("capture: the swapchain's format %d is not one this writes", static_cast<int>(format));
        return false;
    }
    const VkDeviceSize size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
    if (!make_staging(device, phys, size)) {
        host_log("capture: no host-visible memory for a %ux%u frame", extent.width, extent.height);
        return false;
    }
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_staging.buffer, 1, &region);
    VkImageMemoryBarrier back = b;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &back);
    VkBufferMemoryBarrier hb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hb.srcQueueFamilyIndex = hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hb.buffer = g_staging.buffer;
    hb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hb, 0, nullptr);
    g_in_flight = true;
    g_format = format;
    g_extent = extent;
    g_label = upscaler::kind_name(host_settings().upscaler);
    return true;
}

void after_fence(VkDevice device) {
    (void)device;
    if (!g_in_flight) return;
    g_in_flight = false;
    const std::size_t bytes = static_cast<std::size_t>(g_extent.width) * g_extent.height * 4;
    std::vector<std::uint8_t> px(static_cast<const std::uint8_t*>(g_staging.map), static_cast<const std::uint8_t*>(g_staging.map) + bytes);
    g_writing.fetch_add(1);
    std::thread([px = std::move(px), f = g_format, e = g_extent, l = g_label]() mutable {
        write_png(std::move(px), f, e, std::move(l));
        g_writing.fetch_sub(1);
    }).detach();
}

void shutdown(VkDevice device) {
    if (g_staging.buffer || g_staging.memory) destroy_staging(device);
    g_in_flight = false;
}

}  // namespace frame_capture
