// The host overlay's Vulkan side. One pipeline, one host-visible vertex
// buffer, triangles with alpha blending, drawn over the presented frame with
// dynamic rendering so there is no render pass or framebuffer to keep in step
// with the swapchain.

#include "host/overlay.h"
#include "host/trophies.h"
#include "host/gpu.h"

#include "host/overlay_font.h"
#include "host/overlay_ui_font.h"
#include "host/overlay_spv.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

struct Vertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

// Two lists: the window's pump builds the next one in g_verts from
// host_overlay_reset() on, and host_overlay_commit() hands it to the presenter
// as g_shown. With one list a present that landed between the reset and the
// rebuild found it empty, and the FPS counter blinked out for that frame.
// g_shown_count lets a present with nothing to draw find that out without the
// lock: what a hidden overlay costs the presenter is one atomic load.
std::mutex g_mu;
std::vector<Vertex> g_verts;  // being built by the pump (under g_mu)
std::vector<Vertex> g_shown;  // what presents draw (under g_mu)
std::atomic<std::size_t> g_shown_count{0};
std::vector<OverlayGlass> g_glass, g_glass_shown;  // frosted panes, built and shown with the lists (under g_mu)

// Presenter-owned, created once for the swapchain's format.
VkDevice g_device = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE;
VkPipeline g_pipeline = VK_NULL_HANDLE;
VkBuffer g_vbo = VK_NULL_HANDLE;
VkImage g_atlas = VK_NULL_HANDLE;
VkDeviceMemory g_atlas_mem = VK_NULL_HANDLE;
VkImageView g_atlas_view = VK_NULL_HANDLE;
VkSampler g_sampler = VK_NULL_HANDLE;
// Colour pictures (the trophies' icons, host/trophies.h) beside the font
// atlas: a vertex whose u is 2 or more samples them at (u - 2, v). A 1x1
// clear texel stands in when there are none.
VkImage g_pics = VK_NULL_HANDLE;
VkDeviceMemory g_pics_mem = VK_NULL_HANDLE;
VkImageView g_pics_view = VK_NULL_HANDLE;
int g_pics_w = 1, g_pics_h = 1, g_pics_cell = 0, g_pics_cols = 0;  // set before g_pics_ready
std::atomic<bool> g_pics_ready{false};
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
VkDescriptorPool g_dpool = VK_NULL_HANDLE;
VkDescriptorSet g_dset = VK_NULL_HANDLE;
VkDeviceMemory g_vbo_mem = VK_NULL_HANDLE;
void* g_vbo_map = nullptr;
VkDeviceSize g_vbo_bytes = 0;
VkFormat g_format = VK_FORMAT_UNDEFINED;

// rc2: 1 MiB (~21.8k triangles). vk1-dream's F10 bug: hovering an
// Upscaling or Resolution row put its note on screen, and those notes are
// long: the frame's vertices passed 256 KiB and the whole overlay - menu and
// all - was dropped, not cut. Now a list that does not fit draws what fits
// (whole triangles, the menu's panel and rows come first), logged once.
constexpr VkDeviceSize kVboBytes = 1024u * 1024u;

// The atlas texel that is fully lit: a plain rectangle points every corner at
// it, so one pipeline draws rectangles and glyphs alike.
// The texture is overlay_ui_font's atlas: the monospace one (overlay_font) in its
// top-left corner, the menus' proportional fonts below it.
constexpr float kAtlasWf = static_cast<float>(overlay_ui_font::kAtlasW);
constexpr float kAtlasHf = static_cast<float>(overlay_ui_font::kAtlasH);
constexpr float kSolidU = (overlay_ui_font::kSolidX + 0.5f) / kAtlasWf;
constexpr float kSolidV = (overlay_ui_font::kSolidY + 0.5f) / kAtlasHf;
static_assert(overlay_ui_font::kSolidX == overlay_font::kSolidX && overlay_ui_font::kSolidY == overlay_font::kSolidY);

void push(float x, float y, std::uint32_t rgba, float u = kSolidU, float vv = kSolidV) {
    Vertex v;
    v.x = x;
    v.y = y;
    v.u = u;
    v.v = vv;
    v.r = static_cast<float>((rgba >> 24) & 0xff) / 255.0f;
    v.g = static_cast<float>((rgba >> 16) & 0xff) / 255.0f;
    v.b = static_cast<float>((rgba >> 8) & 0xff) / 255.0f;
    v.a = static_cast<float>(rgba & 0xff) / 255.0f;
    g_verts.push_back(v);
}

std::uint32_t memory_type(VkPhysicalDevice phys, std::uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    return UINT32_MAX;
}

VkShaderModule make_module(VkDevice device, const std::uint32_t* words, std::size_t count) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = count * 4;
    ci.pCode = words;
    VkShaderModule m = VK_NULL_HANDLE;
    return vkCreateShaderModule(device, &ci, nullptr, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
}

}  // namespace

void host_overlay_reset() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_verts.clear();
    g_glass.clear();
}

void host_overlay_glass(float x, float y, float w, float h, float radius, float blur, float brightness) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_glass.size() < 16) g_glass.push_back({x, y, w, h, radius, blur, brightness});
}

std::size_t host_overlay_glass_shown(OverlayGlass* out, std::size_t max) {
    std::lock_guard<std::mutex> lk(g_mu);
    const std::size_t n = std::min(max, g_glass_shown.size());
    for (std::size_t i = 0; i < n; ++i) out[i] = g_glass_shown[i];
    return n;
}

void host_overlay_commit() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_shown.swap(g_verts);
    g_glass_shown.swap(g_glass);
    g_shown_count.store(g_shown.size(), std::memory_order_release);
}

std::size_t host_overlay_built_vertices() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_verts.size();
}
std::size_t host_overlay_shown_vertices() { return g_shown_count.load(std::memory_order_acquire); }

bool host_overlay_empty() { return g_shown_count.load(std::memory_order_acquire) == 0; }

void host_overlay_tri(float x0, float y0, float x1, float y1, float x2, float y2, std::uint32_t rgba) {
    std::lock_guard<std::mutex> lk(g_mu);
    push(x0, y0, rgba);
    push(x1, y1, rgba);
    push(x2, y2, rgba);
}

void host_overlay_rect(float x, float y, float w, float h, std::uint32_t rgba) {
    std::lock_guard<std::mutex> lk(g_mu);
    push(x, y, rgba);
    push(x + w, y, rgba);
    push(x, y + h, rgba);
    push(x + w, y, rgba);
    push(x + w, y + h, rgba);
    push(x, y + h, rgba);
}

void host_overlay_cursor(float x, float y) {
    // An arrow, tip at (x, y): the outline first and the fill over it, so the
    // pointer stays visible on a light menu and on a dark one.
    const float s = 22.0f;  // long enough to read at 1080p
    // The tip is (x, y) itself; these are the other two corners, as fractions.
    const struct { float dx, dy; } tail{0.32f, 0.86f}, edge{0.62f, 0.62f};
    const float o = 2.0f;
    host_overlay_tri(x - o, y - o, x + edge.dx * s + o, y + edge.dy * s + o, x + tail.dx * s, y + tail.dy * s + o, 0x000000c0u);
    host_overlay_tri(x, y, x + edge.dx * s, y + edge.dy * s, x + tail.dx * s, y + tail.dy * s, 0xffffffffu);
}

// One sampled image, its pixels uploaded through a one-shot command buffer:
// this runs when the swapchain's format is first known.
bool upload_image(VkDevice device, VkPhysicalDevice phys, VkQueue queue, std::uint32_t family, VkFormat format, std::uint32_t w,
                  std::uint32_t h, const void* pixels, std::size_t bytes, VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ici, nullptr, &image) != VK_SUCCESS) return false;
    VkMemoryRequirements ireq{};
    vkGetImageMemoryRequirements(device, image, &ireq);
    VkMemoryAllocateInfo imai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    imai.allocationSize = ireq.size;
    imai.memoryTypeIndex = memory_type(phys, ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device, &imai, nullptr, &memory) != VK_SUCCESS) return false;
    vkBindImageMemory(device, image, memory, 0);

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkBufferCreateInfo sbci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    sbci.size = bytes;
    sbci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(device, &sbci, nullptr, &staging) != VK_SUCCESS) return false;
    VkMemoryRequirements sreq{};
    vkGetBufferMemoryRequirements(device, staging, &sreq);
    VkMemoryAllocateInfo smai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    smai.allocationSize = sreq.size;
    smai.memoryTypeIndex = memory_type(phys, sreq.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (smai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device, &smai, nullptr, &staging_mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, staging, staging_mem, 0);
    void* map = nullptr;
    if (vkMapMemory(device, staging_mem, 0, sreq.size, 0, &map) != VK_SUCCESS) return false;
    std::memcpy(map, pixels, bytes);
    vkUnmapMemory(device, staging_mem);

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = family;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &cpci, nullptr, &pool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &cbai, &cmd);
    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    host_gpu_queue_lock();
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    host_gpu_queue_unlock();
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, staging_mem, nullptr);

    VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = format;
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &ivci, nullptr, &view) != VK_SUCCESS) return false;
    return true;
}

// Uploads the font atlas and the pictures, and builds the descriptor that
// samples them.
bool upload_atlas(VkDevice device, VkPhysicalDevice phys, VkQueue queue, std::uint32_t family) {
    if (!upload_image(device, phys, queue, family, VK_FORMAT_R8_UNORM, overlay_ui_font::kAtlasW, overlay_ui_font::kAtlasH,
                      overlay_ui_font::kAtlas, sizeof(overlay_ui_font::kAtlas), g_atlas, g_atlas_mem, g_atlas_view))
        return false;
    const std::uint8_t* pics = nullptr;
    int pw = 0, ph = 0, cell = 0, cols = 0;
    static const std::uint8_t kClear[4] = {0, 0, 0, 0};
    const bool have = trophy_icon_sheet(&pics, &pw, &ph, &cell, &cols);
    if (!have) {
        pics = kClear;
        pw = ph = 1;
    }
    if (!upload_image(device, phys, queue, family, VK_FORMAT_R8G8B8A8_UNORM, static_cast<std::uint32_t>(pw), static_cast<std::uint32_t>(ph),
                      pics, static_cast<std::size_t>(pw) * ph * 4, g_pics, g_pics_mem, g_pics_view))
        return false;
    g_pics_w = pw;
    g_pics_h = ph;
    g_pics_cell = have ? cell : 0;
    g_pics_cols = have ? cols : 0;
    g_pics_ready.store(have, std::memory_order_release);
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &sci, nullptr, &g_sampler) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &g_dpool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = g_dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g_dsl;
    if (vkAllocateDescriptorSets(device, &dsai, &g_dset) != VK_SUCCESS) return false;
    const VkDescriptorImageInfo dii[2] = {{g_sampler, g_atlas_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                          {g_sampler, g_pics_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet w[2]{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[i].dstSet = g_dset;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &dii[i];
    }
    vkUpdateDescriptorSets(device, 2, w, 0, nullptr);
    return true;
}

bool host_overlay_picture(float x, float y, float w, float h, int index, std::uint32_t rgba) {
    if (!g_pics_ready.load(std::memory_order_acquire) || index < 0 || g_pics_cols <= 0) return false;
    const int col = index % g_pics_cols, row = index / g_pics_cols;
    if ((row + 1) * g_pics_cell > g_pics_h) return false;
    // Half a texel in from the cell's edge, so filtering never reaches the next icon.
    const float u0 = 2.0f + (col * g_pics_cell + 0.5f) / g_pics_w, u1 = 2.0f + ((col + 1) * g_pics_cell - 0.5f) / g_pics_w;
    const float v0 = (row * g_pics_cell + 0.5f) / g_pics_h, v1 = ((row + 1) * g_pics_cell - 0.5f) / g_pics_h;
    std::lock_guard<std::mutex> lk(g_mu);
    push(x, y, rgba, u0, v0);
    push(x + w, y, rgba, u1, v0);
    push(x, y + h, rgba, u0, v1);
    push(x + w, y, rgba, u1, v0);
    push(x + w, y + h, rgba, u1, v1);
    push(x, y + h, rgba, u0, v1);
    return true;
}

float host_overlay_text_width(float scale, const char* text) {
    if (!text) return 0.0f;
    std::size_t n = 0;
    for (const char* c = text; *c; ++c) ++n;
    return static_cast<float>(n) * overlay_font::kAdvanceW * scale;
}

float host_overlay_text(float x, float y, float scale, std::uint32_t rgba, const char* text) {
    if (!text) return 0.0f;
    // A character advances by the layout box but is drawn as the whole padded
    // cell, offset so the ink lands where the box says. The padding is why the
    // quad is bigger than the advance: without it linear filtering reaches into
    // the neighbouring cell and puts an underscore over every 'o'.
    const float aw = overlay_font::kAdvanceW * scale;
    const float cw = overlay_font::kCellW * scale, ch = overlay_font::kCellH * scale;
    const float ox = overlay_font::kOriginX * scale, oy = overlay_font::kOriginY * scale;
    float pen = x;
    std::lock_guard<std::mutex> lk(g_mu);
    for (const char* c = text; *c; ++c, pen += aw) {
        const unsigned ch_code = static_cast<unsigned char>(*c);
        if (ch_code < overlay_font::kFirst || ch_code > overlay_font::kLast) continue;  // space included, just blank
        const int idx = static_cast<int>(ch_code) - overlay_font::kFirst;
        const float u0 = static_cast<float>((idx % overlay_font::kCols) * overlay_font::kCellW) / kAtlasWf;
        const float v0 = static_cast<float>((idx / overlay_font::kCols) * overlay_font::kCellH) / kAtlasHf;
        const float u1 = u0 + static_cast<float>(overlay_font::kCellW) / kAtlasWf;
        const float v1 = v0 + static_cast<float>(overlay_font::kCellH) / kAtlasHf;
        const float gx = pen - ox, gy = y - oy;
        push(gx, gy, rgba, u0, v0);
        push(gx + cw, gy, rgba, u1, v0);
        push(gx, gy + ch, rgba, u0, v1);
        push(gx + cw, gy, rgba, u1, v0);
        push(gx + cw, gy + ch, rgba, u1, v1);
        push(gx, gy + ch, rgba, u0, v1);
    }
    return pen - x;
}

// ---- Helia's Dream's shapes and fonts (the menus' glass look) ----

namespace {

std::uint32_t lerp_rgba(std::uint32_t a, std::uint32_t b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    std::uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const float ca = static_cast<float>((a >> sh) & 0xff), cb = static_cast<float>((b >> sh) & 0xff);
        out |= static_cast<std::uint32_t>(std::lround(ca + (cb - ca) * t)) << sh;
    }
    return out;
}

// The outline of a rounded rectangle, clockwise from the top-left corner's
// top end: 7 points a corner.
void round_outline(float x, float y, float w, float h, float r, std::vector<std::pair<float, float>>& pts) {
    r = std::max(0.0f, std::min(r, std::min(w, h) * 0.5f));
    constexpr int kSeg = 6;
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r};
    const float cy[4] = {y + r, y + r, y + h - r, y + h - r};
    const float start[4] = {3.14159265f, 4.71238898f, 0.0f, 1.57079633f};
    pts.clear();
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i <= kSeg; ++i) {
            const float a = start[c] + 1.57079633f * static_cast<float>(i) / kSeg;
            pts.emplace_back(cx[c] + std::cos(a) * r, cy[c] + std::sin(a) * r);
        }
}

// The next code point of UTF-8 text, moving `c` past it; a malformed byte is
// itself (and has no glyph).
unsigned next_code(const char*& c) {
    const auto* p = reinterpret_cast<const unsigned char*>(c);
    unsigned code = p[0];
    int more = 0;
    if (code >= 0xf0 && p[1] && p[2] && p[3]) { code &= 0x07; more = 3; }
    else if (code >= 0xe0 && p[1] && p[2]) { code &= 0x0f; more = 2; }
    else if (code >= 0xc0 && p[1]) { code &= 0x1f; more = 1; }
    for (int i = 1; i <= more; ++i) code = (code << 6) | (p[i] & 0x3f);
    c += 1 + more;
    return code;
}

const overlay_ui_font::Face& face_of(int face) {
    switch (face) {
        case kOverlayFontBold: return overlay_ui_font::kBold;
        case kOverlayFontTitle: return overlay_ui_font::kTitle;
        default: return overlay_ui_font::kBody;
    }
}

}  // namespace

void host_overlay_round_rect(float x, float y, float w, float h, float r, std::uint32_t top, std::uint32_t bottom) {
    if (w <= 0.0f || h <= 0.0f) return;
    std::vector<std::pair<float, float>> pts;
    round_outline(x, y, w, h, r, pts);
    const auto col = [&](float py) { return lerp_rgba(top, bottom, (py - y) / h); };
    const float mx = x + w * 0.5f, my = y + h * 0.5f;
    const std::uint32_t mc = col(my);
    std::lock_guard<std::mutex> lk(g_mu);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        push(mx, my, mc);
        push(a.first, a.second, col(a.second));
        push(b.first, b.second, col(b.second));
    }
}

void host_overlay_round_frame(float x, float y, float w, float h, float r, float t, std::uint32_t top, std::uint32_t bottom) {
    if (w <= 2 * t || h <= 2 * t) return;
    std::vector<std::pair<float, float>> outer, inner;
    round_outline(x, y, w, h, r, outer);
    round_outline(x + t, y + t, w - 2 * t, h - 2 * t, std::max(0.0f, r - t), inner);
    const auto col = [&](float py) { return lerp_rgba(top, bottom, (py - y) / h); };
    std::lock_guard<std::mutex> lk(g_mu);
    for (std::size_t i = 0; i < outer.size(); ++i) {
        const std::size_t j = (i + 1) % outer.size();
        const auto &o0 = outer[i], &o1 = outer[j], &i0 = inner[i], &i1 = inner[j];
        push(o0.first, o0.second, col(o0.second));
        push(o1.first, o1.second, col(o1.second));
        push(i0.first, i0.second, col(i0.second));
        push(o1.first, o1.second, col(o1.second));
        push(i1.first, i1.second, col(i1.second));
        push(i0.first, i0.second, col(i0.second));
    }
}

float host_overlay_ui_text_width(int face, float size, const char* text) {
    if (!text) return 0.0f;
    const auto& f = face_of(face);
    const float scale = size / static_cast<float>(f.size);
    float w = 0.0f;
    for (const char* c = text; *c;) {
        const int i = overlay_ui_font::glyph_index(next_code(c));
        if (i >= 0) w += f.glyphs[i].advance * scale;
    }
    return w;
}

float host_overlay_ui_text(int face, float x, float y, float size, std::uint32_t rgba, const char* text) {
    if (!text) return 0.0f;
    const auto& f = face_of(face);
    const float scale = size / static_cast<float>(f.size);
    float pen = x;
    std::lock_guard<std::mutex> lk(g_mu);
    for (const char* c = text; *c;) {
        const int i = overlay_ui_font::glyph_index(next_code(c));
        if (i < 0) continue;
        const auto& g = f.glyphs[i];
        if (g.w && g.h) {
            const float gx = pen + static_cast<float>(g.ox) * scale, gy = y + static_cast<float>(g.oy) * scale;
            const float gw = static_cast<float>(g.w) * scale, gh = static_cast<float>(g.h) * scale;
            const float u0 = static_cast<float>(g.x) / kAtlasWf, v0 = static_cast<float>(g.y) / kAtlasHf;
            const float u1 = static_cast<float>(g.x + g.w) / kAtlasWf, v1 = static_cast<float>(g.y + g.h) / kAtlasHf;
            push(gx, gy, rgba, u0, v0);
            push(gx + gw, gy, rgba, u1, v0);
            push(gx, gy + gh, rgba, u0, v1);
            push(gx + gw, gy, rgba, u1, v0);
            push(gx + gw, gy + gh, rgba, u1, v1);
            push(gx, gy + gh, rgba, u0, v1);
        }
        pen += g.advance * scale;
    }
    return pen - x;
}

bool host_overlay_init(VkDevice device, VkPhysicalDevice phys, VkFormat colour_format, VkQueue queue, std::uint32_t family) {
    if (g_pipeline != VK_NULL_HANDLE && g_format == colour_format && g_device == device) return true;
    host_overlay_shutdown(g_device ? g_device : device);
    g_device = device;
    g_format = colour_format;

    VkDescriptorSetLayoutBinding dslb[2]{};
    for (std::uint32_t i = 0; i < 2; ++i) {  // the font atlas, the pictures
        dslb[i].binding = i;
        dslb[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        dslb[i].descriptorCount = 1;
        dslb[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 2;
    dslci.pBindings = dslb;
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &g_dsl) != VK_SUCCESS) return false;

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g_dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &g_layout) != VK_SUCCESS) return false;

    VkShaderModule vs = make_module(device, overlay_spv::kVert, sizeof(overlay_spv::kVert) / 4);
    VkShaderModule fs = make_module(device, overlay_spv::kFrag, sizeof(overlay_spv::kFrag) / 4);
    if (!vs || !fs) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3]{{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                                               {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2},
                                               {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 4}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    // The target's alpha falls where the overlay covers it (dst * (1 - a)):
    // the HDR encode keeps those pixels apart as UI (host/hdr_ui.h).
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo prci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    prci.colorAttachmentCount = 1;
    prci.pColorAttachmentFormats = &g_format;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &prci;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = g_layout;
    const VkResult r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &g_pipeline);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    if (r != VK_SUCCESS) return false;

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = kVboBytes;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &g_vbo) != VK_SUCCESS) return false;
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, g_vbo, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = memory_type(phys, req.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device, &mai, nullptr, &g_vbo_mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, g_vbo, g_vbo_mem, 0);
    if (vkMapMemory(device, g_vbo_mem, 0, req.size, 0, &g_vbo_map) != VK_SUCCESS) return false;
    g_vbo_bytes = req.size;
    return upload_atlas(device, phys, queue, family);
}

void host_overlay_shutdown(VkDevice device) {
    if (!device) return;
    if (g_vbo_map) vkUnmapMemory(device, g_vbo_mem);
    if (g_vbo) vkDestroyBuffer(device, g_vbo, nullptr);
    if (g_vbo_mem) vkFreeMemory(device, g_vbo_mem, nullptr);
    if (g_dpool) vkDestroyDescriptorPool(device, g_dpool, nullptr);
    if (g_dsl) vkDestroyDescriptorSetLayout(device, g_dsl, nullptr);
    if (g_sampler) vkDestroySampler(device, g_sampler, nullptr);
    if (g_atlas_view) vkDestroyImageView(device, g_atlas_view, nullptr);
    if (g_atlas) vkDestroyImage(device, g_atlas, nullptr);
    if (g_atlas_mem) vkFreeMemory(device, g_atlas_mem, nullptr);
    if (g_pics_view) vkDestroyImageView(device, g_pics_view, nullptr);
    if (g_pics) vkDestroyImage(device, g_pics, nullptr);
    if (g_pics_mem) vkFreeMemory(device, g_pics_mem, nullptr);
    g_pics_ready.store(false, std::memory_order_release);
    g_pics_view = VK_NULL_HANDLE;
    g_pics = VK_NULL_HANDLE;
    g_pics_mem = VK_NULL_HANDLE;
    if (g_pipeline) vkDestroyPipeline(device, g_pipeline, nullptr);
    if (g_layout) vkDestroyPipelineLayout(device, g_layout, nullptr);
    g_vbo_map = nullptr;
    g_vbo = VK_NULL_HANDLE;
    g_vbo_mem = VK_NULL_HANDLE;
    g_pipeline = VK_NULL_HANDLE;
    g_layout = VK_NULL_HANDLE;
    g_dpool = VK_NULL_HANDLE;
    g_dsl = VK_NULL_HANDLE;
    g_sampler = VK_NULL_HANDLE;
    g_atlas_view = VK_NULL_HANDLE;
    g_atlas = VK_NULL_HANDLE;
    g_atlas_mem = VK_NULL_HANDLE;
}

std::uint32_t host_overlay_record(VkCommandBuffer cmd, VkRect2D area, float display_w, float display_h) {
    if (host_overlay_empty() || !g_pipeline || !g_vbo_map || display_w <= 0.0f || display_h <= 0.0f) return 0;
    std::uint32_t count = 0;
    {
        // Straight into the vertex buffer: the presenter's fence says the GPU
        // is done with what the last frame drew from it.
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_shown.empty()) return 0;
        std::size_t n = g_shown.size();
        const std::size_t fit = static_cast<std::size_t>(g_vbo_bytes / sizeof(Vertex)) / 3 * 3;
        if (n > fit) {
            static std::atomic<bool> said{false};
            if (!said.exchange(true))
                host_log("overlay: %zu vertices this frame, the buffer holds %zu: the first %zu are drawn (logged once)", n, fit, fit);
            n = fit;
        }
        std::memcpy(g_vbo_map, g_shown.data(), n * sizeof(Vertex));
        count = static_cast<std::uint32_t>(n);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &g_dset, 0, nullptr);
    VkViewport vp{static_cast<float>(area.offset.x), static_cast<float>(area.offset.y), static_cast<float>(area.extent.width),
                  static_cast<float>(area.extent.height), 0.0f, 1.0f};
    VkRect2D sc = area;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    const float inv[2] = {1.0f / display_w, 1.0f / display_h};
    vkCmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(inv), inv);
    const VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &g_vbo, &off);
    vkCmdDraw(cmd, count, 1, 0, 0);
    return count;
}

bool host_overlay_draw(VkDevice device, VkCommandBuffer cmd, VkImage image, VkImageView view, VkExtent2D extent, VkRect2D area,
                       float display_w, float display_h) {
    (void)image;
    (void)device;
    if (host_overlay_empty() || !g_pipeline || !g_vbo_map || display_w <= 0.0f || display_h <= 0.0f) return false;
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = view;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // The game's frame is already there; the overlay goes over it.
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, extent};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vkCmdBeginRendering(cmd, &ri);
    const bool drew = host_overlay_record(cmd, area, display_w, display_h) != 0;
    vkCmdEndRendering(cmd);
    return drew;
}
