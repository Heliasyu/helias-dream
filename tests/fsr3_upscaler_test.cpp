// heliasdream-vk Phase 3: host/fsr3_upscaler.cpp - FSR 3.1.5 through
// FSR-Vulkan - on whatever Vulkan device there is (lavapipe is enough):
//   - a context is made at every quality preset's render size for a 720p
//     and a 4K output, and its shared images are what the sizes ask;
//   - a few frames of a flat scene upscale to the same flat colour, finite;
//   - a dispatch with a reset (a camera cut) takes the new picture at once.
// Exits 77 (ctest: skipped) without a Vulkan device.
#include "host/fsr3_upscaler.h"
#include "host/upscaler_presets.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

float half_to_float(std::uint16_t h) {
    const std::uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    float v;
    if (e == 0) v = std::ldexp(static_cast<float>(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(static_cast<float>(m | 0x400), static_cast<int>(e) - 25);
    return s ? -v : v;
}

struct Vk {
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
};

std::uint32_t memory_type(VkPhysicalDevice pd, std::uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits >> i & 1) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return ~0u;
}

struct Img {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    UpscalerImage u;
};

Img make_image(const Vk& vk, VkFormat fmt, std::uint32_t w, std::uint32_t h) {
    Img i;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {w, h, 1};
    ci.mipLevels = ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    vkCreateImage(vk.dev, &ci, nullptr, &i.image);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.dev, i.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(vk.pd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(vk.dev, &ai, nullptr, &i.mem);
    vkBindImageMemory(vk.dev, i.image, i.mem, 0);
    i.u = {i.image, fmt, w, h, w, h};
    return i;
}

void barrier(VkCommandBuffer cmd) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void to_general(VkCommandBuffer cmd, VkImage img) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void clear(VkCommandBuffer cmd, VkImage img, float r, float g, float b, float a) {
    VkClearColorValue c{};
    c.float32[0] = r;
    c.float32[1] = g;
    c.float32[2] = b;
    c.float32[3] = a;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_GENERAL, &c, 1, &range);
}

bool init(Vk& vk) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &vk.inst) != VK_SUCCESS) return false;
    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(vk.inst, &n, nullptr);
    if (!n) return false;
    std::vector<VkPhysicalDevice> pds(n);
    vkEnumeratePhysicalDevices(vk.inst, &n, pds.data());
    vk.pd = pds[0];
    std::uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.pd, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(vk.pd, &qn, qf.data());
    vk.family = ~0u;
    for (std::uint32_t i = 0; i < qn; ++i)
        if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            vk.family = i;
            break;
        }
    if (vk.family == ~0u) return false;
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = vk.family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(vk.pd, &features);
    VkPhysicalDeviceFeatures want{};
    want.shaderStorageImageExtendedFormats = features.shaderStorageImageExtendedFormats;
    want.shaderStorageImageWriteWithoutFormat = features.shaderStorageImageWriteWithoutFormat;
    want.shaderStorageImageReadWithoutFormat = features.shaderStorageImageReadWithoutFormat;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.pEnabledFeatures = &want;
    if (vkCreateDevice(vk.pd, &dci, nullptr, &vk.dev) != VK_SUCCESS) return false;
    vkGetDeviceQueue(vk.dev, vk.family, 0, &vk.queue);
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = vk.family;
    return vkCreateCommandPool(vk.dev, &cpci, nullptr, &vk.pool) == VK_SUCCESS;
}

VkCommandBuffer begin(const Vk& vk) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = vk.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(vk.dev, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void submit(const Vk& vk, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(vk.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(vk.queue);
    vkFreeCommandBuffers(vk.dev, vk.pool, 1, &cmd);
}

// The context and its shared images at each preset.
void sizes_per_preset(const Vk& vk, std::uint32_t ow, std::uint32_t oh) {
    for (int p = upscaler::kNativeAa; p < upscaler::kPresetCount; ++p) {
        std::uint32_t rw = 0, rh = 0;
        upscaler::render_size(p, ow, oh, &rw, &rh);
        Fsr3Upscaler f;
        std::string why;
        const bool ok = f.create(vk.pd, vk.dev, rw, rh, ow, oh, &why);
        if (!ok) std::fprintf(stderr, "%s %ux%u -> %ux%u: %s\n", upscaler::preset_name(p), rw, rh, ow, oh, why.c_str());
        CHECK(ok);
        if (!ok) continue;
        // FSR's dilated depth / motion and reconstructed previous depth are render-sized.
        for (int k = 0; k < 3; ++k) {
            const Fsr3Upscaler::SharedInfo s = f.shared_info(k);
            CHECK(s.width == rw && s.height == rh);
            CHECK(s.format != VK_FORMAT_UNDEFINED);
        }
        CHECK(f.memory_bytes() > 0);
        std::printf("  %-17s %4ux%-4u -> %ux%u: shared %ux%u (formats %d %d %d), %.1f MiB\n", upscaler::preset_name(p), rw, rh, ow, oh,
                    f.shared_info(0).width, f.shared_info(0).height, f.shared_info(0).format, f.shared_info(1).format, f.shared_info(2).format,
                    static_cast<double>(f.memory_bytes()) / (1024.0 * 1024.0));
        f.destroy();
    }
}

}  // namespace

int main() {
    Vk vk;
    if (!init(vk)) {
        std::printf("fsr3_upscaler_test: no Vulkan device - skipped\n");
        return 77;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk.pd, &props);
    std::printf("fsr3_upscaler_test on %s\n", props.deviceName);
    sizes_per_preset(vk, 1280, 720);
    sizes_per_preset(vk, 3840, 2160);

    // A few frames at Performance (2x) for a 256x144 output.
    const std::uint32_t OW = 256, OH = 144, RW = 128, RH = 72;
    Fsr3Upscaler f;
    std::string why;
    CHECK(f.create(vk.pd, vk.dev, RW, RH, OW, OH, &why));
    if (!f.ready()) {
        std::fprintf(stderr, "create: %s\n", why.c_str());
        return 1;
    }
    Img color = make_image(vk, VK_FORMAT_R16G16B16A16_SFLOAT, RW, RH);
    Img depth = make_image(vk, VK_FORMAT_R32_SFLOAT, RW, RH);
    Img motion = make_image(vk, VK_FORMAT_R16G16B16A16_SFLOAT, RW, RH);
    Img reactive = make_image(vk, VK_FORMAT_R8_UNORM, RW, RH);
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = static_cast<VkDeviceSize>(OW) * OH * 8;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buf;
    vkCreateBuffer(vk.dev, &bci, nullptr, &buf);
    VkMemoryRequirements breq;
    vkGetBufferMemoryRequirements(vk.dev, buf, &breq);
    VkMemoryAllocateInfo bai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bai.allocationSize = breq.size;
    bai.memoryTypeIndex = memory_type(vk.pd, breq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory bmem;
    vkAllocateMemory(vk.dev, &bai, nullptr, &bmem);
    vkBindBufferMemory(vk.dev, buf, bmem, 0);
    void* map = nullptr;
    vkMapMemory(vk.dev, bmem, 0, bci.size, 0, &map);

    {
        VkCommandBuffer cmd = begin(vk);
        for (VkImage i : {color.image, depth.image, motion.image, reactive.image}) to_general(cmd, i);
        clear(cmd, depth.image, 0.5f, 0, 0, 0);
        clear(cmd, motion.image, 0, 0, 0, 1);
        clear(cmd, reactive.image, 0, 0, 0, 0);
        submit(vk, cmd);
    }
    // Runs one frame of a flat colour and returns the output's mean and its worst distance from `want`.
    std::uint64_t frame_id = 0;
    const auto frame = [&](const float rgb[3], bool reset, double* worst) {
        VkCommandBuffer cmd = begin(vk);
        clear(cmd, color.image, rgb[0], rgb[1], rgb[2], 1.0f);
        barrier(cmd);
        Fsr3Frame fr;
        fr.cmd = cmd;
        fr.color = color.u;
        fr.depth = depth.u;
        fr.motion = motion.u;
        fr.reactive = reactive.u;
        static const float kJitter[4][2] = {{0.0f, -0.1667f}, {-0.25f, 0.1667f}, {0.25f, -0.3889f}, {-0.375f, -0.0556f}};
        fr.jitter_x = kJitter[frame_id % 4][0];
        fr.jitter_y = kJitter[frame_id % 4][1];
        fr.reset = reset;
        fr.sharpen = true;
        fr.sharpness = 0.8f;
        fr.frame_id = ++frame_id;
        std::string w;
        const bool ok = f.record(fr, &w);
        if (!ok) std::fprintf(stderr, "record: %s\n", w.c_str());
        CHECK(ok);
        barrier(cmd);
        VkBufferImageCopy down{};
        down.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        down.imageExtent = {OW, OH, 1};
        vkCmdCopyImageToBuffer(cmd, f.output(), VK_IMAGE_LAYOUT_GENERAL, buf, 1, &down);
        barrier(cmd);
        submit(vk, cmd);
        f.retire(frame_id);
        const auto* out = static_cast<const std::uint16_t*>(map);
        double w0 = 0.0;
        bool finite = true;
        for (std::uint32_t k = 0; k < OW * OH; ++k)
            for (int c = 0; c < 3; ++c) {
                const float v = half_to_float(out[k * 4 + c]);
                finite = finite && std::isfinite(v);
                w0 = std::fmax(w0, std::fabs(v - rgb[c]));
            }
        CHECK(finite);
        *worst = w0;
    };
    const float grey[3] = {0.5f, 0.25f, 0.125f}, bright[3] = {2.0f, 1.5f, 1.0f};
    double worst = 0.0;
    frame(grey, true, &worst);
    std::printf("  frame 1 (reset): worst |out - in| %.4f\n", worst);
    CHECK(worst < 0.02);
    for (int k = 0; k < 4; ++k) frame(grey, false, &worst);
    std::printf("  frame 5: worst %.4f\n", worst);
    CHECK(worst < 0.02);
    // A cut: the next picture with a reset is taken as it is.
    frame(bright, true, &worst);
    std::printf("  a cut to a new picture, with a reset: worst %.4f\n", worst);
    CHECK(worst < 0.05);
    // A small change, with a reset: taken at once. (Without one FSR takes a
    // flat picture's change about as fast - its shading-change test sees
    // the whole screen change - so that is printed, not checked; the reset
    // policy itself is tests/temporal_test.cpp's.)
    const float near_grey[3] = {0.56f, 0.28f, 0.14f};
    for (int k = 0; k < 5; ++k) frame(grey, false, &worst);
    double with_reset = 0.0, without = 0.0;
    frame(near_grey, true, &with_reset);
    for (int k = 0; k < 5; ++k) frame(grey, false, &worst);
    frame(near_grey, false, &without);
    std::printf("  a small change: worst %.4f with a reset, %.4f without\n", with_reset, without);
    CHECK(with_reset < 0.01);
    CHECK(std::isfinite(without));

    f.destroy();
    for (Img* i : {&color, &depth, &motion, &reactive}) {
        vkDestroyImage(vk.dev, i->image, nullptr);
        vkFreeMemory(vk.dev, i->mem, nullptr);
    }
    vkDestroyBuffer(vk.dev, buf, nullptr);
    vkFreeMemory(vk.dev, bmem, nullptr);
    vkDestroyCommandPool(vk.dev, vk.pool, nullptr);
    vkDestroyDevice(vk.dev, nullptr);
    vkDestroyInstance(vk.inst, nullptr);
    if (!g_fail) std::printf("fsr3_upscaler_test: ok\n");
    return g_fail ? 1 : 0;
}
