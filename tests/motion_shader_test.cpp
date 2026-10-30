// heliasdream-vk Phase 2: the motion-vector compute pass (host/shaders/motion.comp,
// as compiled into host/shaders/motion.spv.h) run on whatever Vulkan device
// there is - a software one (lavapipe) is enough - against the same math in
// double (host/temporal/camera.h). A depth image of a known scene, two
// cameras, a jitter; every pixel's motion must match within half-float
// precision. Exits 77 (ctest: skipped) without a Vulkan device.
#include "host/shaders/motion.spv.h"
#include "host/temporal/camera.h"

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace temporal;

namespace {

int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)
#define VK(x)                                                                 \
    do {                                                                      \
        if ((x) != VK_SUCCESS) {                                              \
            std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #x); \
            return 1;                                                         \
        }                                                                     \
    } while (0)

struct Push {
    float reproject[12];
    float proj[4], prev_proj[4];
    float jitter[2];
    float velocity_scale[2];
    std::uint32_t extent[2];
    std::uint32_t flags, pad;
};

float half_to_float(std::uint16_t h) {
    const std::uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    float v;
    if (e == 0) v = std::ldexp(static_cast<float>(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(static_cast<float>(m | 0x400), static_cast<int>(e) - 25);
    return s ? -v : v;
}

SceneCamera camera(double x, double y, double z, double yaw_deg, std::uint32_t w, std::uint32_t h) {
    std::vector<float> f(kSceneConstantFloats, 0.0f);
    f[0] = 3000.0f;
    f[1] = 1.0f / 3000.0f;
    f[4] = static_cast<float>(w);
    f[5] = static_cast<float>(h);
    f[6] = 1.0f / f[4];
    f[7] = 1.0f / f[5];
    const double a = yaw_deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
    const double rows[3][3] = {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};  // world -> view rotation
    const double p[3] = {x, y, z};
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) f[8 + r * 4 + k] = static_cast<float>(rows[r][k]);
        f[8 + r * 4 + 3] = static_cast<float>(-(rows[r][0] * p[0] + rows[r][1] * p[1] + rows[r][2] * p[2]));
    }
    f[52] = 1.42799f;
    f[57] = 2.53865f;
    f[62] = 1.00002f;
    f[63] = -0.0500679f;
    SceneCamera cam;
    parse_scene_constants(f.data(), f.size(), &cam);
    return cam;
}

std::uint32_t memory_type(VkPhysicalDevice pd, std::uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits >> i & 1) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return ~0u;
}

}  // namespace

int main() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance inst;
    if (vkCreateInstance(&ici, nullptr, &inst) != VK_SUCCESS) {
        std::printf("motion_shader_test: no Vulkan instance - skipped\n");
        return 77;
    }
    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    if (!n) {
        std::printf("motion_shader_test: no Vulkan device - skipped\n");
        return 77;
    }
    std::vector<VkPhysicalDevice> pds(n);
    vkEnumeratePhysicalDevices(inst, &n, pds.data());
    VkPhysicalDevice pd = pds[0];
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

    const std::uint32_t W = 320, H = 180;
    // The scene: a floor at y = 0 and a 4-unit wall 20 ahead, sky above, seen from a
    // camera 1.6 above the floor; the previous camera a step back and turned.
    const SceneCamera cur = camera(1834.4, 1.6, -2210.7, 12.0, W, H), prev = camera(1834.3, 1.6, -2210.9, 10.5, W, H);
    const double jit[2] = {0.27, -0.31};
    std::vector<float> depth(W * H);
    Affine inv;
    affine_inverse(cur.view, &inv);
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x) {
            // The ray through the jittered pixel centre, in view space at z = 1.
            const double u = (x + 0.5 - jit[0]) / W, v = (y + 0.5 - jit[1]) / H;
            const double vx = (2 * u - 1) / cur.proj.x, vy = (1 - 2 * v) / cur.proj.y;
            const double o[3] = {0, 0, 0}, d1[3] = {vx, vy, 1};
            double wo[3], wd[3];
            affine_point(inv, o, wo);
            affine_point(inv, d1, wd);
            for (int k = 0; k < 3; ++k) wd[k] -= wo[k];
            double t = 1e30;
            if (wd[1] < -1e-9) t = std::fmin(t, -wo[1] / wd[1]);                        // the floor
            if (wd[2] > 1e-9) {  // the wall, 4 units high
                const double tw = (-2190.0 - wo[2]) / wd[2];
                if (wo[1] + tw * wd[1] < 4.0) t = std::fmin(t, tw);
            }
            const bool sky = !(t < 2000.0);
            depth[y * W + x] = sky ? 1.0f : static_cast<float>(cur.proj.zs + cur.proj.zo / t);  // view z = t (ray z is 1)
        }

    // Images: depth (D32), velocity dummy (RGBA8 1x1), motion (RGBA16F).
    auto make_image = [&](VkFormat fmt, std::uint32_t w, std::uint32_t h, VkImageUsageFlags usage, VkImage* img, VkDeviceMemory* mem) {
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
    VkImage dimg, vimg, mimg, cimg;
    VkDeviceMemory dmem, vmem, mmem, cmem;
    if (!make_image(VK_FORMAT_D32_SFLOAT, W, H, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &dimg, &dmem) ||
        !make_image(VK_FORMAT_R8G8B8A8_UNORM, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT, &vimg, &vmem) ||
        !make_image(VK_FORMAT_R16G16B16A16_SFLOAT, W, H, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &mimg, &mmem) ||
        !make_image(VK_FORMAT_R32_SFLOAT, W, H, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &cimg, &cmem)) {
        std::fprintf(stderr, "image creation failed\n");
        return 1;
    }
    // A host buffer for the depth upload and the motion readback.
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(W) * H * 12;  // motion (8 bytes a pixel), then the depth copy (4)
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
    std::memcpy(map, depth.data(), depth.size() * 4);

    auto view_of = [&](VkImage img, VkFormat fmt, VkImageAspectFlags aspect) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = img;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt;
        vci.subresourceRange = {aspect, 0, 1, 0, 1};
        VkImageView v = VK_NULL_HANDLE;
        vkCreateImageView(dev, &vci, nullptr, &v);
        return v;
    };
    const VkImageView dview = view_of(dimg, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    const VkImageView vview = view_of(vimg, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    const VkImageView mview = view_of(mimg, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
    const VkImageView cview = view_of(cimg, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    VkSampler smp;
    VK(vkCreateSampler(dev, &sci, nullptr, &smp));

    // The pipeline, as host/temporal_inputs.cpp makes it.
    const VkDescriptorSetLayoutBinding binds[5] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo sli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sli.bindingCount = 5;
    sli.pBindings = binds;
    VkDescriptorSetLayout dsl;
    VK(vkCreateDescriptorSetLayout(dev, &sli, nullptr, &dsl));
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &dsl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &range;
    VkPipelineLayout layout;
    VK(vkCreatePipelineLayout(dev, &pli, nullptr, &layout));
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = sizeof(k_motion_spv);
    smi.pCode = k_motion_spv;
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
    const VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 3;
    dpci.pPoolSizes = sizes;
    VkDescriptorPool pool;
    VK(vkCreateDescriptorPool(dev, &dpci, nullptr, &pool));
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;
    VkDescriptorSet set;
    VK(vkAllocateDescriptorSets(dev, &dsai, &set));
    const VkDescriptorImageInfo infos[5] = {{VK_NULL_HANDLE, dview, VK_IMAGE_LAYOUT_GENERAL},
                                            {smp, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
                                            {VK_NULL_HANDLE, vview, VK_IMAGE_LAYOUT_GENERAL},
                                            {VK_NULL_HANDLE, mview, VK_IMAGE_LAYOUT_GENERAL},
                                            {VK_NULL_HANDLE, cview, VK_IMAGE_LAYOUT_GENERAL}};
    const VkDescriptorType types[5] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    VkWriteDescriptorSet wr[5] = {};
    for (int k = 0; k < 5; ++k) {
        wr[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[k].dstSet = set;
        wr[k].dstBinding = static_cast<std::uint32_t>(k);
        wr[k].descriptorCount = 1;
        wr[k].descriptorType = types[k];
        wr[k].pImageInfo = &infos[k];
    }
    vkUpdateDescriptorSets(dev, 5, wr, 0, nullptr);

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = family;
    VkCommandPool cpool;
    VK(vkCreateCommandPool(dev, &cpci, nullptr, &cpool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK(vkBeginCommandBuffer(cmd, &cbbi));
    auto to_layout = [&](VkImage img, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = {aspect, 0, 1, 0, 1};
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    to_layout(dimg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy up{};
    up.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    up.imageExtent = {W, H, 1};
    vkCmdCopyBufferToImage(cmd, buf, dimg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &up);
    to_layout(dimg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    to_layout(vimg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    to_layout(mimg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    to_layout(cimg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    const Reprojection r = make_reprojection(prev, cur);
    Push push{};
    std::memcpy(push.reproject, r.reproject, sizeof(push.reproject));
    std::memcpy(push.proj, r.proj, sizeof(push.proj));
    std::memcpy(push.prev_proj, r.prev_proj, sizeof(push.prev_proj));
    push.jitter[0] = static_cast<float>(jit[0]);
    push.jitter[1] = static_cast<float>(jit[1]);
    push.extent[0] = W;
    push.extent[1] = H;
    push.flags = 1;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (W + 7) / 8, (H + 7) / 8, 1);
    to_layout(mimg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy down{};
    down.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    down.imageExtent = {W, H, 1};
    vkCmdCopyImageToBuffer(cmd, mimg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &down);
    to_layout(cimg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    down.bufferOffset = static_cast<VkDeviceSize>(W) * H * 8;
    vkCmdCopyImageToBuffer(cmd, cimg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &down);
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

    // Compare.
    const auto* out = static_cast<const std::uint16_t*>(map);
    double worst = 0, sum = 0;
    std::uint32_t sky = 0, bad = 0, invalid = 0;
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x) {
            double dx, dy;
            camera_motion(r, x, y, depth[y * W + x], W, H, jit, &dx, &dy);
            const float gx = half_to_float(out[(y * W + x) * 4 + 0]), gy = half_to_float(out[(y * W + x) * 4 + 1]);
            const float valid = half_to_float(out[(y * W + x) * 4 + 3]);
            if (valid < 0.5f) ++invalid;
            sky += depth[y * W + x] >= 1.0f;
            // Half floats: ~3 significant digits.
            const double tol = 0.02 + 0.002 * std::fmax(std::fabs(dx), std::fabs(dy));
            const double e = std::fmax(std::fabs(gx - dx), std::fabs(gy - dy));
            worst = std::fmax(worst, e);
            sum += std::fabs(dx) + std::fabs(dy);
            if (e > tol && bad++ < 5) std::fprintf(stderr, "pixel %u,%u: GPU (%g, %g), CPU (%g, %g)\n", x, y, gx, gy, dx, dy);
        }
    CHECK(bad == 0);
    // The depth copy (binding 4): the depth, bit for bit.
    const auto* copy = reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(map) + static_cast<std::size_t>(W) * H * 8);
    std::uint32_t copy_bad = 0;
    for (std::uint32_t k = 0; k < W * H; ++k) copy_bad += copy[k] != depth[k];
    CHECK(copy_bad == 0);
    CHECK(invalid == 0);
    CHECK(sky > 0 && sky < W * H);  // the test scene has both
    CHECK(sum / (W * H) > 0.5);      // and moves
    std::printf("motion_shader_test: %s: %ux%u pixels match the CPU math (worst %.4f px, mean |motion| %.2f px, %u sky)\n", props.deviceName, W, H,
                worst, sum / (W * H), sky);
    // Everything back before the device goes (clean under VK_LAYER_KHRONOS_validation).
    vkDeviceWaitIdle(dev);
    vkFreeCommandBuffers(dev, cpool, 1, &cmd);
    vkDestroyCommandPool(dev, cpool, nullptr);
    vkDestroyDescriptorPool(dev, pool, nullptr);
    vkDestroyPipeline(dev, pipe, nullptr);
    vkDestroyShaderModule(dev, module, nullptr);
    vkDestroyPipelineLayout(dev, layout, nullptr);
    vkDestroyDescriptorSetLayout(dev, dsl, nullptr);
    vkDestroySampler(dev, smp, nullptr);
    for (VkImageView v : {dview, vview, mview, cview}) vkDestroyImageView(dev, v, nullptr);
    for (VkImage i : {dimg, vimg, mimg, cimg}) vkDestroyImage(dev, i, nullptr);
    for (VkDeviceMemory m : {dmem, vmem, mmem, cmem}) vkFreeMemory(dev, m, nullptr);
    vkUnmapMemory(dev, bmem);
    vkDestroyBuffer(dev, buf, nullptr);
    vkFreeMemory(dev, bmem, nullptr);
    vkDestroyDevice(dev, nullptr);
    vkDestroyInstance(inst, nullptr);
    return g_fail ? 1 : 0;
}
