// heliasdream-vk test7: FSR 4.1.1's sharpening pass (host/shaders/fsr4_sharpen.comp,
// as compiled into host/shaders/fsr4_sharpen.spv.h) on whatever Vulkan device
// there is (lavapipe is enough), against RCAS (AMD's FsrRcasF with noise
// removal) in double on the postpass's log-encoded output, decoded as the
// replay's rcas.spv decodes it (test7b: test7 skipped the decoding, a white
// haze). The image is not a multiple of the shader's 16x16 blocks, so the
// blocks' borders and the image's edges are covered. Exits 77 (ctest:
// skipped) without a device.
#include "host/shaders/fsr4_sharpen.spv.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

#define VK(x)                                                                   \
    do {                                                                        \
        if ((x) != VK_SUCCESS) {                                                \
            std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #x); \
            return 1;                                                           \
        }                                                                       \
    } while (0)

float half_to_float(std::uint16_t h) {
    const std::uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    float v;
    if (e == 0) v = std::ldexp(static_cast<float>(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(static_cast<float>(m | 0x400), static_cast<int>(e) - 25);
    return s ? -v : v;
}
// Round to nearest; normal values only (the test's range).
std::uint16_t float_to_half(float f) {
    if (f == 0.0f) return 0;
    int e;
    const float m = std::frexp(f, &e);  // f = m * 2^e, m in [0.5, 1)
    const int he = e - 1 + 15;
    const auto mant = static_cast<std::uint32_t>(std::lround((m * 2.0f - 1.0f) * 1024.0f));
    return static_cast<std::uint16_t>(((he + (mant >> 10)) << 10) | (mant & 0x3ff));
}

std::uint32_t memory_type(VkPhysicalDevice pd, std::uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits >> i & 1) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return ~0u;
}

struct Rgb {
    double r, g, b;
};
// The postpass's encoding (rcas.spv's constants): v = log2(1 + 150 c) / 12.2887.
double encode(double c) { return std::log2(1.0 + 150.0 * c) / 12.2887039; }
constexpr float kExposure = 2.0f, kPreExposure = 0.5f;  // decoded colour * pre / exposure

// FsrRcasF (fsr1/ffx_fsr1.h) with FSR_RCAS_DENOISE, exact reciprocals.
Rgb rcas(Rgb b, Rgb d, Rgb e, Rgb f, Rgb h, double con) {
    auto luma = [](Rgb c) { return c.b * 0.5 + (c.r * 0.5 + c.g); };
    const double bL = luma(b), dL = luma(d), eL = luma(e), fL = luma(f), hL = luma(h);
    double nz = 0.25 * (bL + dL + fL + hL) - eL;
    const double range = std::max({bL, dL, eL, fL, hL}) - std::min({bL, dL, eL, fL, hL});
    nz = std::clamp(std::fabs(nz) / range, 0.0, 1.0);
    if (!std::isfinite(nz)) nz = 1.0;  // flat: the shader's approximate rcp of 0 is huge, saturated
    nz = -0.5 * nz + 1.0;
    double lobe_max = -1e30;
    const double ch[5][3] = {{b.r, b.g, b.b}, {d.r, d.g, d.b}, {e.r, e.g, e.b}, {f.r, f.g, f.b}, {h.r, h.g, h.b}};
    for (int c = 0; c < 3; ++c) {
        const double mn4 = std::min({ch[0][c], ch[1][c], ch[3][c], ch[4][c]});
        const double mx4 = std::max({ch[0][c], ch[1][c], ch[3][c], ch[4][c]});
        const double hit_min = std::min(mn4, ch[2][c]) / (4.0 * mx4);
        const double hit_max = (1.0 - std::max(mx4, ch[2][c])) / (4.0 * mn4 - 4.0);
        lobe_max = std::max(lobe_max, std::max(-hit_min, hit_max));
    }
    double lobe = std::max(-(0.25 - 1.0 / 16.0), std::min(lobe_max, 0.0)) * con;
    lobe *= nz;
    const double rcp = 1.0 / (4.0 * lobe + 1.0);
    return {(lobe * (b.r + d.r + h.r + f.r) + e.r) * rcp, (lobe * (b.g + d.g + h.g + f.g) + e.g) * rcp,
            (lobe * (b.b + d.b + h.b + f.b) + e.b) * rcp};
}

}  // namespace

int main() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance inst;
    if (vkCreateInstance(&ici, nullptr, &inst) != VK_SUCCESS) {
        std::printf("fsr4_sharpen_test: no Vulkan instance - skipped\n");
        return 77;
    }
    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    if (!n) {
        std::printf("fsr4_sharpen_test: no Vulkan device - skipped\n");
        return 77;
    }
    std::vector<VkPhysicalDevice> pds(n);
    vkEnumeratePhysicalDevices(inst, &n, pds.data());
    const VkPhysicalDevice pd = pds[0];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    std::uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf.data());
    std::uint32_t family = ~0u;
    for (std::uint32_t i = 0; i < qn; ++i)
        if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            family = i;
            break;
        }
    if (family == ~0u) return 77;
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    VkDevice dev;
    VK(vkCreateDevice(pd, &dci, nullptr, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, family, 0, &queue);

    // 53x37: three blocks by three, the last ones partial. Smooth gradients,
    // hard edges, noise and a few HDR highlights (a lamp at 60).
    const std::uint32_t W = 53, H = 37;
    std::vector<std::uint16_t> in(W * H * 4);
    std::vector<float> src(W * H * 4);
    std::vector<double> lin_luma(W * H);  // the linear input's, for the brightness check
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> noise(0.0f, 0.3f);
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x) {
            float* p = &src[(y * W + x) * 4];
            const float base = (x / 8 + y / 8) % 2 ? 0.8f : 0.1f;
            p[0] = base + noise(rng);
            p[1] = 0.5f * base + 0.02f * x;
            p[2] = 0.3f + 0.03f * y + noise(rng);
            if ((x == 20 && y == 17) || (x == 47 && y == 15) || (x == 16 && y == 32)) p[0] = p[1] = p[2] = 30.0f;  // a lamp
            lin_luma[y * W + x] = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            for (int c = 0; c < 3; ++c) p[c] = static_cast<float>(encode(p[c]));  // as the postpass leaves it
            p[3] = 0.25f + (x % 3) * 0.25f;
            for (int c = 0; c < 4; ++c) {
                in[(y * W + x) * 4 + c] = float_to_half(p[c]);
                p[c] = half_to_float(in[(y * W + x) * 4 + c]);
            }
        }

    auto make_image = [&](VkImage* img, VkDeviceMemory* mem, VkImageUsageFlags usage, VkFormat fmt = VK_FORMAT_R16G16B16A16_SFLOAT,
                          std::uint32_t w = W, std::uint32_t h = H) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = fmt;
        ci.extent = {w, h, 1};
        ci.mipLevels = ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = usage;
        if (vkCreateImage(dev, &ci, nullptr, img) != VK_SUCCESS) return false;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(dev, *img, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memory_type(pd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        return vkAllocateMemory(dev, &ai, nullptr, mem) == VK_SUCCESS && vkBindImageMemory(dev, *img, *mem, 0) == VK_SUCCESS;
    };
    VkImage simg, dimg, eimg;
    VkDeviceMemory smem, dmem, emem;
    if (!make_image(&simg, &smem, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !make_image(&dimg, &dmem, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        !make_image(&eimg, &emem, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_FORMAT_R32_SFLOAT, 2, 1)) {
        std::fprintf(stderr, "image creation failed\n");
        return 1;
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(W) * H * 8;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buf;
    VK(vkCreateBuffer(dev, &bci, nullptr, &buf));
    VkMemoryRequirements breq;
    vkGetBufferMemoryRequirements(dev, buf, &breq);
    VkMemoryAllocateInfo bai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bai.allocationSize = breq.size;
    bai.memoryTypeIndex = memory_type(pd, breq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory bmem;
    VK(vkAllocateMemory(dev, &bai, nullptr, &bmem));
    VK(vkBindBufferMemory(dev, buf, bmem, 0));
    void* map;
    VK(vkMapMemory(dev, bmem, 0, bytes, 0, &map));
    std::memcpy(map, in.data(), bytes);

    auto view_of = [&](VkImage img, VkFormat fmt = VK_FORMAT_R16G16B16A16_SFLOAT) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = img;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v = VK_NULL_HANDLE;
        vkCreateImageView(dev, &vci, nullptr, &v);
        return v;
    };
    const VkImageView sview = view_of(simg), dview = view_of(dimg), eview = view_of(eimg, VK_FORMAT_R32_SFLOAT);

    // The pipeline, as host/fsr4_pass.cpp makes it.
    const VkDescriptorSetLayoutBinding binds[3] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo sli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sli.bindingCount = 3;
    sli.pBindings = binds;
    VkDescriptorSetLayout dsl;
    VK(vkCreateDescriptorSetLayout(dev, &sli, nullptr, &dsl));
    struct Push {
        std::uint32_t con0[4];
        std::uint32_t size[2];
        float pre_exposure;
    };
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &dsl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &range;
    VkPipelineLayout layout;
    VK(vkCreatePipelineLayout(dev, &pli, nullptr, &layout));
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = sizeof(k_fsr4_sharpen_spv);
    smi.pCode = k_fsr4_sharpen_spv;
    VkShaderModule module;
    VK(vkCreateShaderModule(dev, &smi, nullptr, &module));
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = module;
    cpi.stage.pName = "main";
    cpi.layout = layout;
    VkPipeline pipe;
    VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipe));
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &size;
    VkDescriptorPool pool;
    VK(vkCreateDescriptorPool(dev, &dpci, nullptr, &pool));
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;
    VkDescriptorSet set;
    VK(vkAllocateDescriptorSets(dev, &dsai, &set));
    const VkDescriptorImageInfo infos[3] = {{VK_NULL_HANDLE, sview, VK_IMAGE_LAYOUT_GENERAL},
                                            {VK_NULL_HANDLE, dview, VK_IMAGE_LAYOUT_GENERAL},
                                            {VK_NULL_HANDLE, eview, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet wr[3] = {};
    for (int k = 0; k < 3; ++k) {
        wr[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[k].dstSet = set;
        wr[k].dstBinding = static_cast<std::uint32_t>(k);
        wr[k].descriptorCount = 1;
        wr[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wr[k].pImageInfo = &infos[k];
    }
    vkUpdateDescriptorSets(dev, 3, wr, 0, nullptr);

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = family;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool cpool;
    VK(vkCreateCommandPool(dev, &cpci, nullptr, &cpool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK(vkAllocateCommandBuffers(dev, &cbai, &cmd));

    int failures = 0;
    for (const float sharpness : {0.0f, 0.5f, 1.0f}) {
        std::memcpy(map, in.data(), bytes);
        VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK(vkBeginCommandBuffer(cmd, &cbbi));
        auto to_layout = [&](VkImage img, VkImageLayout from, VkImageLayout to) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.oldLayout = from;
            b.newLayout = to;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = img;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        to_layout(simg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy up{};
        up.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        up.imageExtent = {W, H, 1};
        vkCmdCopyBufferToImage(cmd, buf, simg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &up);
        to_layout(simg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        to_layout(dimg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        to_layout(eimg, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        VkClearColorValue ev{};
        ev.float32[0] = kExposure;
        const VkImageSubresourceRange er{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, eimg, VK_IMAGE_LAYOUT_GENERAL, &ev, 1, &er);
        to_layout(eimg, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        // As fsr4_pass.cpp: stops = 2 - 2 * sharpness, con = 2^-stops.
        const float con = std::exp2(-(2.0f - 2.0f * sharpness));
        Push push{};
        std::memcpy(&push.con0[0], &con, 4);
        push.size[0] = W;
        push.size[1] = H;
        push.pre_exposure = kPreExposure;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (W + 15) / 16, (H + 15) / 16, 1);
        to_layout(dimg, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdCopyImageToBuffer(cmd, dimg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &up);
        VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
        VK(vkEndCommandBuffer(cmd));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
        VK(vkQueueWaitIdle(queue));

        const auto* out = static_cast<const std::uint16_t*>(map);
        auto at = [&](int x, int y) {
            x = std::clamp(x, 0, static_cast<int>(W) - 1);
            y = std::clamp(y, 0, static_cast<int>(H) - 1);
            const float* p = &src[(y * W + x) * 4];
            return Rgb{p[0], p[1], p[2]};
        };
        // The output back in the encoded domain, to compare there (where RCAS ran).
        const double inv = static_cast<double>(kPreExposure) / kExposure;
        auto reencode = [&](float c) { return encode(c / inv); };
        double worst = 0, moved = 0, in_sum = 0, out_sum = 0;
        int bad = 0;
        for (int y = 0; y < static_cast<int>(H); ++y)
            for (int x = 0; x < static_cast<int>(W); ++x) {
                const Rgb want = rcas(at(x, y - 1), at(x - 1, y), at(x, y), at(x + 1, y), at(x, y + 1), con);
                const std::uint16_t* o = &out[(y * W + x) * 4];
                const Rgb got{reencode(half_to_float(o[0])), reencode(half_to_float(o[1])), reencode(half_to_float(o[2]))};
                const Rgb e = at(x, y);
                const double err = std::max({std::fabs(got.r - want.r), std::fabs(got.g - want.g), std::fabs(got.b - want.b)});
                worst = std::max(worst, err);
                moved = std::max(moved, std::max({std::fabs(got.r - e.r), std::fabs(got.g - e.g), std::fabs(got.b - e.b)}));
                // Linear, as the next pass sees it (scaled back by the exposure the decode applied).
                out_sum += (0.2126 * half_to_float(o[0]) + 0.7152 * half_to_float(o[1]) + 0.0722 * half_to_float(o[2])) / inv;
                in_sum += lin_luma[y * W + x];
                const bool alpha_ok = o[3] == o[0];  // alpha is red, as the replay's RCAS writes it
                // Half floats and the shader's approximate reciprocals.
                if ((err > 0.01 || !alpha_ok) && bad++ < 5)
                    std::fprintf(stderr, "sharpness %.1f pixel %d,%d: GPU (%g %g %g), CPU (%g %g %g)%s\n", sharpness, x, y, got.r, got.g,
                                 got.b, want.r, want.g, want.b, alpha_ok ? "" : ", alpha differs");
            }
        if (bad) ++failures;
        // test7b's haze: the mean brightness must survive the pass (HDR lamps
        // included), within 1% where the user plays (sharpness 0 to 0.5).
        const double ratio = out_sum / in_sum;
        if (sharpness <= 0.5f && std::fabs(ratio - 1.0) > 0.01) {
            std::fprintf(stderr, "sharpness %.1f: mean luma %.4f out of %.4f in (%.2f%%)\n", sharpness, out_sum / (W * H), in_sum / (W * H),
                         (ratio - 1.0) * 100.0);
            ++failures;
        }
        if (sharpness >= 0.5f && moved < 0.02) {  // it must sharpen at all
            std::fprintf(stderr, "sharpness %.1f: the output is the input\n", sharpness);
            ++failures;
        }
        std::printf("fsr4_sharpen_test: %s: sharpness %.1f, %ux%u pixels match RCAS (worst %.4f, largest change %.3f, mean luma %+.2f%%)\n",
                    props.deviceName, sharpness, W, H, worst, moved, (ratio - 1.0) * 100.0);
        VK(vkResetCommandBuffer(cmd, 0));
    }

    vkDeviceWaitIdle(dev);
    vkFreeCommandBuffers(dev, cpool, 1, &cmd);
    vkDestroyCommandPool(dev, cpool, nullptr);
    vkDestroyDescriptorPool(dev, pool, nullptr);
    vkDestroyPipeline(dev, pipe, nullptr);
    vkDestroyShaderModule(dev, module, nullptr);
    vkDestroyPipelineLayout(dev, layout, nullptr);
    vkDestroyDescriptorSetLayout(dev, dsl, nullptr);
    for (VkImageView v : {sview, dview, eview}) vkDestroyImageView(dev, v, nullptr);
    for (VkImage i : {simg, dimg, eimg}) vkDestroyImage(dev, i, nullptr);
    for (VkDeviceMemory m : {smem, dmem, emem}) vkFreeMemory(dev, m, nullptr);
    vkUnmapMemory(dev, bmem);
    vkDestroyBuffer(dev, buf, nullptr);
    vkFreeMemory(dev, bmem, nullptr);
    vkDestroyDevice(dev, nullptr);
    vkDestroyInstance(inst, nullptr);
    return failures ? 1 : 0;
}
