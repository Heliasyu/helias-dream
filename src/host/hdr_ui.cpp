// Helia's Dream: HDR with the UI kept apart (host/hdr_ui.h).
#include "host/hdr_ui.h"

#include "hle/modules.h"
#include "host/gpu_internal.h"
#include "host/options.h"
#include "host/settings.h"
#include "log.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace gpu {
namespace {

struct Copy {
    VkImage image = VK_NULL_HANDLE;
    ImageMemory memory;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t w = 0, h = 0;
    std::uint64_t flip = ~0ull;  // the flip it was taken in
    bool general = false;
    bool valid = false;  // a display buffer's: holds its current frame's copy
    bool used = false;   // a UI target's: already handed to a display buffer
};
std::unordered_map<std::uint64_t, Copy> g_before_ui;  // by the UI target's address
std::unordered_map<std::uint64_t, Copy> g_display;    // by the display buffer's address
std::mutex g_present_mu;                              // g_display, against the presenter's reads

// An image of `format` and size, as the copy's destination (GENERAL).
bool fit(Copy& c, VkFormat format, std::uint32_t w, std::uint32_t h) {
    if (c.image && c.format == format && c.w == w && c.h == h) return true;
    if (c.image) defer_destroy_image(c.image, c.memory);
    c = Copy{};
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(g.device, &ci, nullptr, &c.image) != VK_SUCCESS) {
        c = Copy{};
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(g.device, c.image, &req);
    if (!image_memory_alloc(req, c.memory)) {
        vkDestroyImage(g.device, c.image, nullptr);
        c = Copy{};
        return false;
    }
    vkBindImageMemory(g.device, c.image, c.memory.memory, c.memory.offset);
    c.format = format;
    c.w = w;
    c.h = h;
    return true;
}

void copy(VkCommandBuffer cmd, VkImage from, Copy& to) {
    VkImageMemoryBarrier b[2]{};
    for (auto& x : b) {
        x.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        x.srcQueueFamilyIndex = x.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    b[0].image = from;
    b[0].oldLayout = b[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[1].image = to.image;
    b[1].oldLayout = to.general ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    b[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, b);
    to.general = true;
    VkImageCopy r{};
    r.srcSubresource = r.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.extent = {to.w, to.h, 1};
    vkCmdCopyImage(cmd, from, VK_IMAGE_LAYOUT_GENERAL, to.image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
    VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    after.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, nullptr, 0, nullptr);
}

std::atomic<bool> g_said{false};

}  // namespace

bool hdr_ui_wanted() {
    static std::uint64_t serial = ~0ull;
    static bool want = false;
    const std::uint64_t now = host_opt_serial();
    if (now != serial) {
        serial = now;
        const HostSettings s = host_settings();
        want = s.hdr && s.hdr_ui_brightness > 0.0f;
    }
    return want;
}

void hdr_ui_before_ui_locked(RtImage* target) {
    if (!target || !target->image || !target->initialised || target->depth || target->layers != 1) return;
    Copy& c = g_before_ui[target->base];
    const std::uint64_t flip = hle_video_flip_count();
    if (c.flip == flip) return;  // once a frame: the first UI draw
    if (!fit(c, target->format, target->width, target->height)) return;
    begin_recording_locked();
    render_end_pass_locked();
    copy(g_cmd(), target->image, c);
    c.flip = flip;
    c.used = false;
    if (!g_said.exchange(true))
        host_log("hdr: UI kept apart - the frame copied before its first UI draw (%ux%u, format %d)", c.w, c.h, static_cast<int>(c.format));
}

void hdr_ui_display_written_locked(RtImage* display, RtImage* source) {
    if (!display) return;
    const std::uint64_t flip = hle_video_flip_count();
    auto it = source ? g_before_ui.find(source->base) : g_before_ui.end();
    // The copy of the frame this display buffer now gets: taken this flip or
    // the one before (the game copies the finished frame in at the start of
    // the next one), and not handed on already.
    const bool have = it != g_before_ui.end() && !it->second.used && it->second.image && flip - it->second.flip <= 2;
    std::lock_guard<std::mutex> lk(g_present_mu);
    Copy& d = g_display[display->base];
    if (!have) {
        // Another draw into it in the same flip (a fade, the cursor) keeps
        // the copy it already has; a frame written without one has none.
        if (d.flip != flip) d.valid = false;
        return;
    }
    if (!fit(d, it->second.format, it->second.w, it->second.h)) {
        d.valid = false;
        return;
    }
    begin_recording_locked();
    render_end_pass_locked();
    copy(g_cmd(), it->second.image, d);
    it->second.used = true;
    d.flip = flip;
    d.valid = true;
}

}  // namespace gpu

bool host_gpu_hdr_ui_source(std::uint64_t va, VkImage* image, VkFormat* format, std::uint32_t* width, std::uint32_t* height) {
    using namespace gpu;
    std::lock_guard<std::mutex> lk(g_present_mu);
    auto it = g_display.find(va);
    if (it == g_display.end() || !it->second.valid || !it->second.image) return false;
    *image = it->second.image;
    *format = it->second.format;
    *width = it->second.w;
    *height = it->second.h;
    return true;
}
