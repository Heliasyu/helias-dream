// heliasdream-vk: host/fsr411's failure paths on whatever Vulkan device there is
// (lavapipe is enough), the ones a tester without (or with broken) FSR 4.1.1
// assets takes before heliasdream falls back to FSR 3.1:
//   - no asset directory: Record fails, says what is missing;
//   - a pass that is not SPIR-V: Record fails, nothing left behind;
//   - every pass a real compute shader, no initializer.bin: Record fails
//     after the pipelines were made, which are destroyed again;
//   - an initializer.bin too: the images and buffers are made, the first
//     pass names a resource it does not know, Record fails; and
//   - a size it does not do.
// Each failure is retried (a resize asks again) and the upscaler destroyed;
// run under VK_LAYER_KHRONOS_validation this checks nothing leaks or is
// destroyed while in use. No FSR 4.1.1 assets are needed (or shipped): every
// pass is a stand-in shader that writes one image.
// Exits 77 (ctest: skipped) without a Vulkan device.
#include "host/fsr411/fsr411.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

// The stand-in pass (glslc --target-env=vulkan1.2 of):
//   #version 450
//   layout(local_size_x = 64) in;
//   layout(binding = 0, r32f) uniform writeonly image2D rw_not_fsr4;
//   void main() { imageStore(rw_not_fsr4, ivec2(gl_GlobalInvocationID.xy), vec4(0.0)); }
const std::uint32_t kStandIn[] = {
    0x07230203,0x00010500,0x000d000b,0x0000001b,
    0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,
    0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0007000f,0x00000005,0x00000004,0x6e69616d,
    0x00000000,0x00000009,0x0000000e,0x00060010,
    0x00000004,0x00000011,0x00000040,0x00000001,
    0x00000001,0x00030003,0x00000002,0x000001c2,
    0x000a0004,0x475f4c47,0x4c474f4f,0x70635f45,
    0x74735f70,0x5f656c79,0x656e696c,0x7269645f,
    0x69746365,0x00006576,0x00080004,0x475f4c47,
    0x4c474f4f,0x6e695f45,0x64756c63,0x69645f65,
    0x74636572,0x00657669,0x00040005,0x00000004,
    0x6e69616d,0x00000000,0x00050005,0x00000009,
    0x6e5f7772,0x665f746f,0x00347273,0x00080005,
    0x0000000e,0x475f6c67,0x61626f6c,0x766e496c,
    0x7461636f,0x496e6f69,0x00000044,0x00030047,
    0x00000009,0x00000019,0x00040047,0x00000009,
    0x00000021,0x00000000,0x00040047,0x00000009,
    0x00000022,0x00000000,0x00040047,0x0000000e,
    0x0000000b,0x0000001c,0x00040047,0x0000001a,
    0x0000000b,0x00000019,0x00020013,0x00000002,
    0x00030021,0x00000003,0x00000002,0x00030016,
    0x00000006,0x00000020,0x00090019,0x00000007,
    0x00000006,0x00000001,0x00000000,0x00000000,
    0x00000000,0x00000002,0x00000003,0x00040020,
    0x00000008,0x00000000,0x00000007,0x0004003b,
    0x00000008,0x00000009,0x00000000,0x00040015,
    0x0000000b,0x00000020,0x00000000,0x00040017,
    0x0000000c,0x0000000b,0x00000003,0x00040020,
    0x0000000d,0x00000001,0x0000000c,0x0004003b,
    0x0000000d,0x0000000e,0x00000001,0x00040017,
    0x0000000f,0x0000000b,0x00000002,0x00040015,
    0x00000012,0x00000020,0x00000001,0x00040017,
    0x00000013,0x00000012,0x00000002,0x00040017,
    0x00000015,0x00000006,0x00000004,0x0004002b,
    0x00000006,0x00000016,0x00000000,0x0007002c,
    0x00000015,0x00000017,0x00000016,0x00000016,
    0x00000016,0x00000016,0x0004002b,0x0000000b,
    0x00000018,0x00000040,0x0004002b,0x0000000b,
    0x00000019,0x00000001,0x0006002c,0x0000000c,
    0x0000001a,0x00000018,0x00000019,0x00000019,
    0x00050036,0x00000002,0x00000004,0x00000000,
    0x00000003,0x000200f8,0x00000005,0x0004003d,
    0x00000007,0x0000000a,0x00000009,0x0004003d,
    0x0000000c,0x00000010,0x0000000e,0x0007004f,
    0x0000000f,0x00000011,0x00000010,0x00000010,
    0x00000000,0x00000001,0x0004007c,0x00000013,
    0x00000014,0x00000011,0x00040063,0x0000000a,
    0x00000014,0x00000017,0x000100fd,0x00010038
};

const char* const kPasses[] = {
    "spd",         "prepass",     "pass0_post",  "pass1",       "pass1_post",  "pass2",
    "pass2_post",  "pass3",       "pass3_post",  "pass4",       "pass4_post",  "pass5",
    "pass5_post",  "pass6",       "pass6_post",  "pass7",       "pass7_post",  "pass8",
    "pass8_post",  "pass9",       "pass9_post",  "pass10",      "pass10_post", "pass11",
    "pass11_post", "pass12",      "pass12_post", "postpass",    "rcas",
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
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
};

Img make_image(VkPhysicalDevice pd, VkDevice dev, VkFormat fmt, std::uint32_t w, std::uint32_t h, VkImageAspectFlags aspect,
               VkImageUsageFlags usage) {
    Img i;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {w, h, 1};
    ci.mipLevels = ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.usage = usage;
    if (vkCreateImage(dev, &ci, nullptr, &i.image) != VK_SUCCESS) return i;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, i.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(pd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(dev, &ai, nullptr, &i.mem);
    vkBindImageMemory(dev, i.image, i.mem, 0);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = i.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange = {aspect, 0, 1, 0, 1};
    vkCreateImageView(dev, &vci, nullptr, &i.view);
    return i;
}

void destroy(VkDevice dev, Img& i) {
    vkDestroyImageView(dev, i.view, nullptr);
    vkDestroyImage(dev, i.image, nullptr);
    vkFreeMemory(dev, i.mem, nullptr);
    i = {};
}

bool has_extension(VkPhysicalDevice pd, const char* name) {
    std::uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> ext(n);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, ext.data());
    for (const auto& e : ext)
        if (!std::strcmp(e.extensionName, name)) return true;
    return false;
}

void write_file(const std::filesystem::path& p, const void* data, std::size_t size) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

}  // namespace

int main() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ici, nullptr, &inst) != VK_SUCCESS) {
        std::printf("fsr411_fallback_test: no Vulkan; skipped\n");
        return 77;
    }
    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    if (!n) {
        std::printf("fsr411_fallback_test: no Vulkan device; skipped\n");
        vkDestroyInstance(inst, nullptr);
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
    for (std::uint32_t i = 0; i < qn && family == ~0u; ++i)
        if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) family = i;
    // FSR 4.1.1 needs push descriptors before it looks at the assets.
    const bool push = has_extension(pd, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (family == ~0u || !push) {
        std::printf("fsr411_fallback_test: %s has no compute queue or VK_KHR_push_descriptor; skipped\n", props.deviceName);
        vkDestroyInstance(inst, nullptr);
        return 77;
    }
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char* exts[] = {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    VkPhysicalDeviceFeatures have{};
    vkGetPhysicalDeviceFeatures(pd, &have);
    VkPhysicalDeviceFeatures want{};
    want.shaderStorageImageExtendedFormats = have.shaderStorageImageExtendedFormats;
    want.shaderStorageImageWriteWithoutFormat = have.shaderStorageImageWriteWithoutFormat;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = exts;
    dci.pEnabledFeatures = &want;
    VkDevice dev = VK_NULL_HANDLE;
    if (vkCreateDevice(pd, &dci, nullptr, &dev) != VK_SUCCESS) {
        std::fprintf(stderr, "fsr411_fallback_test: no device\n");
        vkDestroyInstance(inst, nullptr);
        return 1;
    }
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = family;
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(dev, &pci, nullptr, &pool);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev, &cai, &cmd);

    // 960x540 -> 1920x1080: the t1080_m0 set (INT8: no FP8 features asked for).
    constexpr std::uint32_t RW = 960, RH = 540, OW = 1920, OH = 1080;
    const VkImageUsageFlags in_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    Img color = make_image(pd, dev, VK_FORMAT_R16G16B16A16_SFLOAT, RW, RH, VK_IMAGE_ASPECT_COLOR_BIT, in_usage);
    Img depth = make_image(pd, dev, VK_FORMAT_R32_SFLOAT, RW, RH, VK_IMAGE_ASPECT_COLOR_BIT, in_usage);
    Img motion = make_image(pd, dev, VK_FORMAT_R16G16_SFLOAT, RW, RH, VK_IMAGE_ASPECT_COLOR_BIT, in_usage);
    Img output = make_image(pd, dev, VK_FORMAT_R16G16B16A16_SFLOAT, OW, OH, VK_IMAGE_ASPECT_COLOR_BIT, in_usage);
    CHECK(color.view && depth.view && motion.view && output.view);
    Fsr411::Frame fr;
    fr.cmdbuf = cmd;
    fr.color = {color.image, color.view, RW, RH};
    fr.depth = {depth.image, depth.view, RW, RH};
    fr.motion = {motion.image, motion.view, RW, RH};
    fr.output = {output.image, output.view, OW, OH};
    fr.render_width = RW;
    fr.render_height = RH;

    const auto dir = std::filesystem::temp_directory_path() / ("fsr411_fallback_test." + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    // One case: a fresh upscaler, two tries (as a resize would ask again), destroyed.
    const auto expect_failure = [&](const char* what, const char* says) {
        Fsr411::Upscaler up(pd, dev, dir.string());
        for (int k = 0; k < 2; ++k) {
            vkResetCommandPool(dev, pool, 0);
            VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vkBeginCommandBuffer(cmd, &bi);
            const bool ok = up.Record(fr);
            vkEndCommandBuffer(cmd);
            CHECK(!ok);
            CHECK(up.Error().find(says) != std::string::npos);
            if (k == 0) std::printf("  %s: %s\n", what, up.Error().c_str());
        }
        vkResetCommandPool(dev, pool, 0);  // it recorded into it: let go of its objects first
    };
    expect_failure("no assets", "missing");
    write_file(dir / "t1080_m0" / "spd.spv", "not a shader", 12);
    expect_failure("a pass that is not SPIR-V", "not SPIR-V");
    for (const char* p : kPasses) write_file(dir / "t1080_m0" / (std::string(p) + ".spv"), kStandIn, sizeof(kStandIn));
    expect_failure("passes but no initializer.bin", "initializer.bin");
    const std::vector<std::uint8_t> init(131072, 0);
    write_file(dir / "t1080_m0" / "initializer.bin", init.data(), init.size());
    expect_failure("a pass with resources it does not know", "unknown resource");
    fr.render_width = OW * 2;
    expect_failure("a size it does not do", "unsupported size");
    std::filesystem::remove_all(dir);

    vkDeviceWaitIdle(dev);
    for (Img* i : {&color, &depth, &motion, &output}) destroy(dev, *i);
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
    vkDestroyCommandPool(dev, pool, nullptr);
    vkDestroyDevice(dev, nullptr);
    vkDestroyInstance(inst, nullptr);
    std::printf("fsr411_fallback_test: %s: %s\n", props.deviceName, g_fail ? "FAIL" : "pass");
    return g_fail ? 1 : 0;
}
