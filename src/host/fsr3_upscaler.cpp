// heliasdream-vk Phase 3: FSR 3.1.5 through FireBurn/FSR-Vulkan's opaque C API
// (host/fsr3_upscaler.h). The bridge's conventions are FSR-Vulkan's
// (third_party/FSR-Vulkan/include/ffx_vk_fsr3_3_1_5_bridge.h, and its
// tests/fsr3_3_1_5_bridge_test.cpp, which this follows for imports): the
// application owns every image FSR is given, including the three shared
// images the context describes; it imports them with their layout and
// abstract state, and the bridge restores both after a dispatch.
#include "host/fsr3_upscaler.h"

#include "ffx_vk_fsr3_3_1_5_bridge.h"

#include <cstdio>

namespace {

std::uint32_t memory_type(VkPhysicalDevice phys, std::uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits >> i & 1u) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if (bits >> i & 1u) return i;
    return ~0u;
}

const char* result_name(FfxVkFsr3_3_1_5Result r) {
    switch (r) {
    case FFX_VK_FSR3_3_1_5_OK: return "ok";
    case FFX_VK_FSR3_3_1_5_ERROR_INVALID_ARGUMENT: return "invalid argument";
    case FFX_VK_FSR3_3_1_5_ERROR_OUT_OF_MEMORY: return "out of memory";
    case FFX_VK_FSR3_3_1_5_ERROR_BACKEND: return "backend error";
    }
    return "unknown error";
}

void set_why(std::string* why, const char* text) {
    if (why) *why = text;
}

}  // namespace

Fsr3Upscaler::~Fsr3Upscaler() { destroy(); }

bool Fsr3Upscaler::make_owned(Owned& o, VkFormat format, std::uint32_t w, std::uint32_t h, std::string* why) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ci, nullptr, &o.image) != VK_SUCCESS) {
        o.image = VK_NULL_HANDLE;
        set_why(why, "an image could not be created");
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, o.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(phys_, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == ~0u || vkAllocateMemory(device_, &ai, nullptr, &o.memory) != VK_SUCCESS ||
        vkBindImageMemory(device_, o.image, o.memory, 0) != VK_SUCCESS) {
        set_why(why, "out of video memory for an image");
        return false;
    }
    o.format = format;
    o.w = w;
    o.h = h;
    memory_bytes_ += req.size;
    FfxVkFsr3_3_1_5ImportedImageDescription d{};
    d.image = o.image;
    d.format = format;
    d.width = w;
    d.height = h;
    d.mipCount = 1;
    d.arrayLayers = 1;
    d.layout = VK_IMAGE_LAYOUT_GENERAL;
    d.state = FFX_VK_FSR3_3_1_5_RESOURCE_STATE_UNORDERED_ACCESS;
    d.usage = ci.usage;
    o.token = ffxVkFsr3_3_1_5BridgeImportImage(ffxVkFsr3_3_1_5UpscalerContextGetBridge(context_), &d).resource;
    if (!o.token) {
        set_why(why, "FSR-Vulkan would not import an image it owns");
        return false;
    }
    return true;
}

bool Fsr3Upscaler::create(VkPhysicalDevice phys, VkDevice device, std::uint32_t render_w, std::uint32_t render_h, std::uint32_t output_w,
                          std::uint32_t output_h, std::string* why) {
    destroy();
    phys_ = phys;
    device_ = device;
    FfxVkFsr3_3_1_5UpscalerCreateInfo ci{};
    ci.physicalDevice = phys;
    ci.device = device;
    ci.maxRenderWidth = render_w;
    ci.maxRenderHeight = render_h;
    ci.maxUpscaleWidth = output_w;
    ci.maxUpscaleHeight = output_h;
    // The game's scene is HDR (RGBA16F, linear, past 1.0), and the shader
    // tables FSR-Vulkan embeds are its HDR permutation; exposure is FSR's own.
    ci.hdrColorInput = VK_TRUE;
    ci.autoExposure = VK_TRUE;
    const FfxVkFsr3_3_1_5Result r = ffxVkFsr3_3_1_5UpscalerContextCreate(&ci, &context_);
    if (r != FFX_VK_FSR3_3_1_5_OK || !context_) {
        context_ = nullptr;
        if (why) *why = std::string("the FSR 3.1.5 context could not be made: ") + result_name(r);
        return false;
    }
    FfxVkFsr3_3_1_5MemoryUsage usage{};
    if (ffxVkFsr3_3_1_5UpscalerContextGetMemoryUsage(context_, &usage) == FFX_VK_FSR3_3_1_5_OK) memory_bytes_ = usage.totalUsageInBytes;
    FfxVkFsr3_3_1_5SharedResourceDescriptions shared{};
    if (ffxVkFsr3_3_1_5UpscalerContextGetSharedResourceDescriptions(context_, &shared) != FFX_VK_FSR3_3_1_5_OK) {
        set_why(why, "FSR 3.1.5 did not describe its shared images");
        destroy();
        return false;
    }
    const FfxVkFsr3_3_1_5SharedResourceDescription* descs[3] = {&shared.dilatedDepth, &shared.dilatedMotionVectors,
                                                                &shared.reconstructedPrevNearestDepth};
    for (int k = 0; k < 3; ++k) {
        if (descs[k]->format == VK_FORMAT_UNDEFINED || !descs[k]->width || !descs[k]->height ||
            !make_owned(shared_[k], descs[k]->format, descs[k]->width, descs[k]->height, why)) {
            if (why && why->empty()) *why = "FSR 3.1.5 asked for a shared image of an unknown format";
            destroy();
            return false;
        }
    }
    if (!make_owned(output_, VK_FORMAT_R16G16B16A16_SFLOAT, output_w, output_h, why)) {
        destroy();
        return false;
    }
    render_w_ = render_w;
    render_h_ = render_h;
    output_w_ = output_w;
    output_h_ = output_h;
    owned_general_ = false;
    return true;
}

void* Fsr3Upscaler::import(const UpscalerImage& img, bool writable) {
    if (!img.image || !img.width || !img.height) return nullptr;
    FfxVkFsr3_3_1_5ImportedImageDescription d{};
    d.image = img.image;
    d.format = img.format;
    d.width = img.width;
    d.height = img.height;
    d.mipCount = 1;
    d.arrayLayers = 1;
    d.layout = VK_IMAGE_LAYOUT_GENERAL;
    d.state = writable ? FFX_VK_FSR3_3_1_5_RESOURCE_STATE_UNORDERED_ACCESS : FFX_VK_FSR3_3_1_5_RESOURCE_STATE_COMPUTE_READ;
    d.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (writable ? VK_IMAGE_USAGE_STORAGE_BIT : 0u);
    void* token = ffxVkFsr3_3_1_5BridgeImportImage(ffxVkFsr3_3_1_5UpscalerContextGetBridge(context_), &d).resource;
    if (token) imports_.push_back({token, 0});
    return token;
}

bool Fsr3Upscaler::record(const Fsr3Frame& f, std::string* why) {
    if (!context_) {
        set_why(why, "no context");
        return false;
    }
    if (!f.cmd || !f.frame_id || f.frame_id < last_frame_) {
        set_why(why, "no command buffer, or a frame id out of order");
        return false;
    }
    const std::size_t first_import = imports_.size();
    void* color = import(f.color, false);
    void* depth = import(f.depth, false);
    void* motion = import(f.motion, false);
    void* reactive = f.reactive.image ? import(f.reactive, false) : nullptr;
    void* direct = f.output.image ? import(f.output, true) : nullptr;
    for (std::size_t k = first_import; k < imports_.size(); ++k) imports_[k].frame_id = f.frame_id;
    if (!color || !depth || !motion || (f.reactive.image && !reactive) || (f.output.image && !direct)) {
        // Nothing recorded: what was imported goes now.
        FfxVkFsr3_3_1_5Bridge* bridge = ffxVkFsr3_3_1_5UpscalerContextGetBridge(context_);
        for (std::size_t k = first_import; k < imports_.size(); ++k) ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, {imports_[k].token});
        imports_.resize(first_import);
        set_why(why, !color ? "the scene colour's format cannot be imported" : !depth ? "the depth copy could not be imported"
                     : !motion ? "the motion vectors could not be imported"
                     : !direct && f.output.image ? "the scale pass's target could not be imported as the output"
                                                 : "the reactive mask could not be imported");
        return false;
    }
    if (!owned_general_) {
        // The owned images' first use: UNDEFINED to GENERAL (their contents
        // start undefined; the first dispatch resets the history).
        VkImageMemoryBarrier b[4] = {};
        const Owned* owned[4] = {&shared_[0], &shared_[1], &shared_[2], &output_};
        for (int k = 0; k < 4; ++k) {
            b[k].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b[k].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b[k].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b[k].srcQueueFamilyIndex = b[k].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b[k].image = owned[k]->image;
            b[k].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b[k].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 4, b);
        owned_general_ = true;
    }
    FfxVkFsr3_3_1_5UpscalerDispatchInfo d{};
    d.commandBuffer = f.cmd;
    d.color = {color};
    d.depth = {depth};
    d.motionVectors = {motion};
    d.reactive = {reactive};
    d.dilatedDepth = {shared_[0].token};
    d.dilatedMotionVectors = {shared_[1].token};
    d.reconstructedPrevNearestDepth = {shared_[2].token};
    d.output = {direct ? direct : output_.token};
    d.jitterOffsetX = f.jitter_x;
    d.jitterOffsetY = f.jitter_y;
    d.motionVectorScaleX = f.mv_scale_x;
    d.motionVectorScaleY = f.mv_scale_y;
    d.renderWidth = render_w_;
    d.renderHeight = render_h_;
    d.upscaleWidth = output_w_;
    d.upscaleHeight = output_h_;
    d.frameTimeMilliseconds = f.frame_ms > 0.0f ? f.frame_ms : 16.7f;
    d.preExposure = 1.0f;
    d.enableSharpening = f.sharpen ? VK_TRUE : VK_FALSE;
    d.sharpness = f.sharpness;
    d.reset = f.reset ? VK_TRUE : VK_FALSE;
    d.cameraNear = f.near_plane;
    d.cameraFar = f.far_plane;
    d.cameraVerticalFovRadians = f.fov_y;
    d.viewSpaceToMeters = 1.0f;
    d.flags = 0;
    d.frameId = f.frame_id;
    const FfxVkFsr3_3_1_5Result r = ffxVkFsr3_3_1_5UpscalerContextRecordDispatch(context_, &d);
    last_frame_ = f.frame_id;
    if (r != FFX_VK_FSR3_3_1_5_OK) {
        // The command buffer may hold part of it: the imports wait for the frame to retire.
        if (why) *why = std::string("the FSR 3.1.5 dispatch failed: ") + result_name(r);
        return false;
    }
    return true;
}

void Fsr3Upscaler::retire(std::uint64_t completed_id) {
    if (!context_) return;
    ffxVkFsr3_3_1_5UpscalerContextRetireFrame(context_, completed_id);
    FfxVkFsr3_3_1_5Bridge* bridge = ffxVkFsr3_3_1_5UpscalerContextGetBridge(context_);
    std::size_t kept = 0;
    for (const Imported& i : imports_) {
        if (i.frame_id && i.frame_id <= completed_id) ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, {i.token});
        else imports_[kept++] = i;
    }
    imports_.resize(kept);
}

void Fsr3Upscaler::destroy() {
    if (context_) {
        FfxVkFsr3_3_1_5Bridge* bridge = ffxVkFsr3_3_1_5UpscalerContextGetBridge(context_);
        ffxVkFsr3_3_1_5UpscalerContextRetireFrame(context_, ~0ull);
        for (const Imported& i : imports_) ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, {i.token});
        for (Owned* o : {&shared_[0], &shared_[1], &shared_[2], &output_})
            if (o->token) ffxVkFsr3_3_1_5BridgeReleaseImportedImage(bridge, {o->token});
        ffxVkFsr3_3_1_5UpscalerContextDestroy(context_);
        context_ = nullptr;
    }
    imports_.clear();
    for (Owned* o : {&shared_[0], &shared_[1], &shared_[2], &output_}) {
        if (o->image) vkDestroyImage(device_, o->image, nullptr);
        if (o->memory) vkFreeMemory(device_, o->memory, nullptr);
        *o = Owned{};
    }
    owned_general_ = false;
    render_w_ = render_h_ = output_w_ = output_h_ = 0;
    last_frame_ = 0;
    memory_bytes_ = 0;
}
