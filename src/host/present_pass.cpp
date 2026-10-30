// The presenter's one pass over the swapchain image (host/present_pass.h).
// One pipeline, one descriptor set for the display image, dynamic rendering
// so nothing has to follow the swapchain's images but their views.

#include "host/present_pass.h"

#include "host/present_spv.h"
#include "host/shaders/motion_view_frag.spv.h"
#include "host/shaders/frosted_frag.spv.h"
#include "host/shaders/present_grade_frag.spv.h"
#include "host/shaders/hdr_encode_frag.spv.h"
#include "host/overlay.h"
#include "log.h"

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace {

VkDevice g_device = VK_NULL_HANDLE;
VkPhysicalDevice g_phys = VK_NULL_HANDLE;
VkFormat g_format = VK_FORMAT_UNDEFINED;  // the swapchain's, the pipeline's attachment
VkDescriptorSetLayout g_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE;
VkPipeline g_pipeline = VK_NULL_HANDLE;
VkPipeline g_sharpen_pipeline = VK_NULL_HANDLE;  // rc2: present_sharpen.frag (HELIAS_SHARPEN_AT_PRESENT=1)
std::atomic<float> g_sharpen_con{0.0f};          // the last request's, 0: none
std::atomic<std::int64_t> g_sharpen_at{0};       // steady ns of that request (FSR's recording thread)
std::int64_t steady_now_ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
VkSampler g_sampler = VK_NULL_HANDLE;
VkDescriptorPool g_pool = VK_NULL_HANDLE;
VkDescriptorSet g_set = VK_NULL_HANDLE;
// The display image's view for this frame. Made each frame, as FSR's is: the
// game's display images are render targets the renderer re-creates when the
// picture's size changes, and a view kept across frames could outlive its
// image (a handle the driver then hands to the next image made).
VkImageView g_source_view = VK_NULL_HANDLE;
// heliasdream-vk Phase 2: the motion-vector view's pipeline (the same vertex
// stage, its own fragment stage, blended over the picture), set and view.
VkPipeline g_motion_pipeline = VK_NULL_HANDLE;
VkDescriptorPool g_motion_pool = VK_NULL_HANDLE;
VkDescriptorSet g_motion_set = VK_NULL_HANDLE;
VkSampler g_point = VK_NULL_HANDLE;
VkImageView g_motion_view = VK_NULL_HANDLE;
// Helia's Dream: the frosted glass under the menus (shaders/frosted.frag),
// blended over the picture, sampling it through g_set.
VkPipeline g_glass_pipeline = VK_NULL_HANDLE;
bool g_source_bound = false;  // g_set holds this frame's picture
// Helia's Dream: the player's grade (present_grade.frag) for the picture, set
// each frame; the plain copy while it is the game's own.
VkPipeline g_grade_pipeline = VK_NULL_HANDLE;
std::atomic<float> g_grade_b{1.0f}, g_grade_c{1.0f}, g_grade_h{0.0f};
// HDR output: the pass renders into g_hdr_image (the swapchain's format, SDR
// values), and present_pass_end encodes it into the swapchain image
// (hdr_encode.frag). Levels set each frame.
VkPipeline g_hdr_pipeline = VK_NULL_HANDLE;
std::atomic<bool> g_hdr_on{false};
std::atomic<float> g_hdr_paper{200.0f}, g_hdr_peak{1000.0f}, g_hdr_boost{0.25f};
VkImage g_hdr_image = VK_NULL_HANDLE;
VkDeviceMemory g_hdr_mem = VK_NULL_HANDLE;
VkImageView g_hdr_view = VK_NULL_HANDLE;
VkExtent2D g_hdr_extent{0, 0};
VkDescriptorPool g_hdr_pool = VK_NULL_HANDLE;
VkDescriptorSet g_hdr_set = VK_NULL_HANDLE;
bool g_encoding = false;  // this frame's pass draws into g_hdr_image
// Helia's Dream, the UI kept apart (host/hdr_ui.h): the encode's own set -
// the SDR layer, the display buffer as the picture drew it, and its copy from
// before its UI - and layout (a larger push range). This frame's copy, if any.
VkDescriptorSetLayout g_hdr_dsl = VK_NULL_HANDLE;
VkPipelineLayout g_hdr_layout = VK_NULL_HANDLE;
VkImageView g_preui_view = VK_NULL_HANDLE;
PresentSource g_preui{};
bool g_preui_set = false;
std::atomic<float> g_ui_brightness{0.0f};
VkRect2D g_shown{};
PresentSource g_src_region{};
VkImage g_dst_image = VK_NULL_HANDLE;
VkImageView g_dst_view = VK_NULL_HANDLE;
VkExtent2D g_dst_extent{0, 0};
// The last source format asked about, and whether it filters linearly.
VkFormat g_checked = VK_FORMAT_UNDEFINED;
bool g_checked_ok = false;

VkShaderModule make_module(VkDevice device, const std::uint32_t* words, std::size_t count) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = count * 4;
    ci.pCode = words;
    VkShaderModule m = VK_NULL_HANDLE;
    return vkCreateShaderModule(device, &ci, nullptr, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
}

bool build(VkDevice device) {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings = &b;
    if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &g_dsl) != VK_SUCCESS) return false;

    const VkPushConstantRange pcrs[2] = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4},
                                         {VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(float) * 4, sizeof(float) * 8}};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g_dsl;
    plci.pushConstantRangeCount = 2;
    plci.pPushConstantRanges = pcrs;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &g_layout) != VK_SUCCESS) return false;

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;  // vkCmdBlitImage's VK_FILTER_LINEAR
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 0.0f;
    if (vkCreateSampler(device, &sci, nullptr, &g_sampler) != VK_SUCCESS) return false;

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &g_pool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = g_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g_dsl;
    if (vkAllocateDescriptorSets(device, &dsai, &g_set) != VK_SUCCESS) return false;

    VkShaderModule vs = make_module(device, present_spv::kVert, sizeof(present_spv::kVert) / 4);
    VkShaderModule fs = make_module(device, present_spv::kFrag, sizeof(present_spv::kFrag) / 4);
    if (!vs || !fs) {
        if (vs) vkDestroyShaderModule(device, vs, nullptr);
        if (fs) vkDestroyShaderModule(device, fs, nullptr);
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
    cba.colorWriteMask = 0xf;  // every channel, as the blit wrote them
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
    if (r == VK_SUCCESS) {  // The grade and the HDR encode: the same fixed-function state, unblended.
        if (VkShaderModule m = make_module(device, k_present_grade_frag_spv, sizeof(k_present_grade_frag_spv) / 4)) {
            stages[1].module = m;
            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &g_grade_pipeline) != VK_SUCCESS) g_grade_pipeline = VK_NULL_HANDLE;
            vkDestroyShaderModule(device, m, nullptr);
        }
        VkDescriptorSetLayoutBinding hb[3]{};
        for (std::uint32_t i = 0; i < 3; ++i) hb[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo hdl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        hdl.bindingCount = 3;
        hdl.pBindings = hb;
        const VkPushConstantRange hpc[2] = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4},
                                            {VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(float) * 4, sizeof(float) * 16}};
        VkPipelineLayoutCreateInfo hpl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        hpl.setLayoutCount = 1;
        hpl.pSetLayouts = &g_hdr_dsl;
        hpl.pushConstantRangeCount = 2;
        hpl.pPushConstantRanges = hpc;
        if (vkCreateDescriptorSetLayout(device, &hdl, nullptr, &g_hdr_dsl) == VK_SUCCESS &&
            vkCreatePipelineLayout(device, &hpl, nullptr, &g_hdr_layout) == VK_SUCCESS) {
            if (VkShaderModule m = make_module(device, k_hdr_encode_frag_spv, sizeof(k_hdr_encode_frag_spv) / 4)) {
                stages[1].module = m;
                gp.layout = g_hdr_layout;
                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &g_hdr_pipeline) != VK_SUCCESS) g_hdr_pipeline = VK_NULL_HANDLE;
                gp.layout = g_layout;
                vkDestroyShaderModule(device, m, nullptr);
            }
        }
        stages[1].module = fs;
        VkDescriptorPoolSize hps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
        VkDescriptorPoolCreateInfo hp = dpci;
        hp.pPoolSizes = &hps;
        if (g_hdr_dsl && vkCreateDescriptorPool(device, &hp, nullptr, &g_hdr_pool) == VK_SUCCESS) {
            VkDescriptorSetAllocateInfo hsa = dsai;
            hsa.descriptorPool = g_hdr_pool;
            hsa.pSetLayouts = &g_hdr_dsl;
            if (vkAllocateDescriptorSets(device, &hsa, &g_hdr_set) != VK_SUCCESS) g_hdr_set = VK_NULL_HANDLE;
        }
    }
    if (r == VK_SUCCESS) {  // rc2: its sharpening variant; its failure costs only that
        if (VkShaderModule sfs = make_module(device, present_spv::kFragSharpen, sizeof(present_spv::kFragSharpen) / 4)) {
            stages[1].module = sfs;
            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &g_sharpen_pipeline) != VK_SUCCESS)
                g_sharpen_pipeline = VK_NULL_HANDLE;
            stages[1].module = fs;
            vkDestroyShaderModule(device, sfs, nullptr);
        }
    }
    // The motion-vector view: blended (its alpha over the picture). Its
    // failure costs only the view.
    VkShaderModule mfs = make_module(device, k_motion_view_frag_spv, sizeof(k_motion_view_frag_spv) / 4);
    if (r == VK_SUCCESS && mfs) {
        stages[1].module = mfs;
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &g_motion_pipeline) != VK_SUCCESS) g_motion_pipeline = VK_NULL_HANDLE;
        sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
        if (vkCreateSampler(device, &sci, nullptr, &g_point) != VK_SUCCESS) g_point = VK_NULL_HANDLE;
        VkDescriptorPoolCreateInfo mp = dpci;
        if (vkCreateDescriptorPool(device, &mp, nullptr, &g_motion_pool) == VK_SUCCESS) {
            dsai.descriptorPool = g_motion_pool;
            if (vkAllocateDescriptorSets(device, &dsai, &g_motion_set) != VK_SUCCESS) g_motion_set = VK_NULL_HANDLE;
        }
    }
    if (mfs) vkDestroyShaderModule(device, mfs, nullptr);
    // The frosted glass: blended by its rounded mask (alpha). Its failure
    // costs only the blur; the menus then show on their own tint.
    if (r == VK_SUCCESS) {
        if (VkShaderModule gfs = make_module(device, k_frosted_frag_spv, sizeof(k_frosted_frag_spv) / 4)) {
            stages[1].module = gfs;
            cba.blendEnable = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &g_glass_pipeline) != VK_SUCCESS) g_glass_pipeline = VK_NULL_HANDLE;
            vkDestroyShaderModule(device, gfs, nullptr);
        }
    }
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    return r == VK_SUCCESS;
}

bool source_filters(VkFormat format) {
    if (format != g_checked) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(g_phys, format, &fp);
        const VkFormatFeatureFlags want = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        g_checked = format;
        g_checked_ok = (fp.optimalTilingFeatures & want) == want;
        if (!g_checked_ok) host_log("present: the display format %d does not filter linearly; it is blitted", static_cast<int>(format));
    }
    return g_checked_ok;
}

}  // namespace

bool present_pass_init(VkDevice device, VkPhysicalDevice phys, VkFormat colour_format) {
    if (g_pipeline != VK_NULL_HANDLE && g_format == colour_format && g_device == device) return true;
    present_pass_shutdown(g_device ? g_device : device);
    g_device = device;
    g_phys = phys;
    g_format = colour_format;
    if (!build(device)) {
        host_log("present: the presenting pass could not be made; the picture is blitted");
        present_pass_shutdown(device);  // tried again with the next swapchain
        return false;
    }
    return true;
}

void present_pass_shutdown(VkDevice device) {
    if (!device) return;
    if (g_source_view) vkDestroyImageView(device, g_source_view, nullptr);
    if (g_pipeline) vkDestroyPipeline(device, g_pipeline, nullptr);
    if (g_sharpen_pipeline) vkDestroyPipeline(device, g_sharpen_pipeline, nullptr);
    g_sharpen_pipeline = VK_NULL_HANDLE;
    if (g_motion_view) vkDestroyImageView(device, g_motion_view, nullptr);
    if (g_motion_pipeline) vkDestroyPipeline(device, g_motion_pipeline, nullptr);
    if (g_glass_pipeline) vkDestroyPipeline(device, g_glass_pipeline, nullptr);
    g_glass_pipeline = VK_NULL_HANDLE;
    if (g_grade_pipeline) vkDestroyPipeline(device, g_grade_pipeline, nullptr);
    if (g_hdr_pipeline) vkDestroyPipeline(device, g_hdr_pipeline, nullptr);
    if (g_hdr_view) vkDestroyImageView(device, g_hdr_view, nullptr);
    if (g_hdr_image) vkDestroyImage(device, g_hdr_image, nullptr);
    if (g_hdr_mem) vkFreeMemory(device, g_hdr_mem, nullptr);
    if (g_hdr_pool) vkDestroyDescriptorPool(device, g_hdr_pool, nullptr);
    if (g_hdr_layout) vkDestroyPipelineLayout(device, g_hdr_layout, nullptr);
    if (g_hdr_dsl) vkDestroyDescriptorSetLayout(device, g_hdr_dsl, nullptr);
    if (g_preui_view) vkDestroyImageView(device, g_preui_view, nullptr);
    g_hdr_layout = VK_NULL_HANDLE;
    g_hdr_dsl = VK_NULL_HANDLE;
    g_preui_view = VK_NULL_HANDLE;
    g_grade_pipeline = g_hdr_pipeline = VK_NULL_HANDLE;
    g_hdr_view = VK_NULL_HANDLE;
    g_hdr_image = VK_NULL_HANDLE;
    g_hdr_mem = VK_NULL_HANDLE;
    g_hdr_pool = VK_NULL_HANDLE;
    g_hdr_set = VK_NULL_HANDLE;
    g_hdr_extent = {0, 0};
    if (g_motion_pool) vkDestroyDescriptorPool(device, g_motion_pool, nullptr);
    if (g_point) vkDestroySampler(device, g_point, nullptr);
    g_motion_view = VK_NULL_HANDLE;
    g_motion_pipeline = VK_NULL_HANDLE;
    g_motion_pool = VK_NULL_HANDLE;
    g_motion_set = VK_NULL_HANDLE;
    g_point = VK_NULL_HANDLE;
    if (g_layout) vkDestroyPipelineLayout(device, g_layout, nullptr);
    if (g_pool) vkDestroyDescriptorPool(device, g_pool, nullptr);
    if (g_dsl) vkDestroyDescriptorSetLayout(device, g_dsl, nullptr);
    if (g_sampler) vkDestroySampler(device, g_sampler, nullptr);
    g_source_view = VK_NULL_HANDLE;
    g_pipeline = VK_NULL_HANDLE;
    g_layout = VK_NULL_HANDLE;
    g_pool = VK_NULL_HANDLE;
    g_set = VK_NULL_HANDLE;
    g_dsl = VK_NULL_HANDLE;
    g_sampler = VK_NULL_HANDLE;
    g_device = VK_NULL_HANDLE;
    g_format = VK_FORMAT_UNDEFINED;
    g_checked = VK_FORMAT_UNDEFINED;
}

bool present_pass_ready() { return g_pipeline != VK_NULL_HANDLE; }

bool present_sharpen_at_present() {
    static const bool on = [] {
        const char* e = std::getenv("HELIAS_SHARPEN_AT_PRESENT");
        const bool v = e && e[0] == '1';
        if (v) host_log("present: HELIAS_SHARPEN_AT_PRESENT=1: FSR's sharpening is done in the presenting pass (the HUD is sharpened too)");
        return v;
    }();
    return on;
}

bool present_pass_set_sharpen(float sharpness) {
    if (sharpness <= 0.0f || !g_sharpen_pipeline) {
        g_sharpen_con.store(0.0f);
        return false;
    }
    const float s = sharpness > 1.0f ? 1.0f : sharpness;
    g_sharpen_con.store(std::exp2(-(2.0f - 2.0f * s)));
    g_sharpen_at.store(steady_now_ns());
    return true;
}

void present_pass_set_grade(float brightness, float contrast, float hue_degrees) {
    g_grade_b.store(brightness);
    g_grade_c.store(contrast);
    g_grade_h.store(hue_degrees);
}

void present_pass_set_hdr(bool on, float paper_white, float peak, float boost) {
    g_hdr_on.store(on);
    g_hdr_paper.store(paper_white);
    g_hdr_peak.store(peak);
    g_hdr_boost.store(boost);
}

namespace {
// The SDR layer HDR output composes into: the swapchain's size and format, so
// every pipeline of the pass (the overlay's, the menu's) draws into it as into
// the swapchain image. Made again when the size changes.
bool hdr_target(VkExtent2D extent) {
    if (!g_hdr_pipeline || !g_hdr_set) return false;
    if (g_hdr_image && g_hdr_extent.width == extent.width && g_hdr_extent.height == extent.height) return true;
    if (g_hdr_view) vkDestroyImageView(g_device, g_hdr_view, nullptr);
    if (g_hdr_image) vkDestroyImage(g_device, g_hdr_image, nullptr);
    if (g_hdr_mem) vkFreeMemory(g_device, g_hdr_mem, nullptr);
    g_hdr_view = VK_NULL_HANDLE;
    g_hdr_image = VK_NULL_HANDLE;
    g_hdr_mem = VK_NULL_HANDLE;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = g_format;
    ici.extent = {extent.width, extent.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g_device, &ici, nullptr, &g_hdr_image) != VK_SUCCESS) {
        g_hdr_image = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(g_device, g_hdr_image, &req);
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(g_phys, &mp);
    std::uint32_t type = UINT32_MAX;
    for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            type = i;
            break;
        }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (type == UINT32_MAX || vkAllocateMemory(g_device, &mai, nullptr, &g_hdr_mem) != VK_SUCCESS) {
        vkDestroyImage(g_device, g_hdr_image, nullptr);
        g_hdr_image = VK_NULL_HANDLE;
        g_hdr_mem = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(g_device, g_hdr_image, g_hdr_mem, 0);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = g_hdr_image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = g_format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(g_device, &vci, nullptr, &g_hdr_view) != VK_SUCCESS) {
        g_hdr_view = VK_NULL_HANDLE;
        return false;
    }
    VkDescriptorImageInfo dii{g_sampler, g_hdr_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = g_hdr_set;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &dii;
    vkUpdateDescriptorSets(g_device, 1, &w, 0, nullptr);
    g_hdr_extent = extent;
    host_log("present: HDR output composes into a %ux%u layer and encodes it as HDR10", extent.width, extent.height);
    return true;
}
}  // namespace

bool present_pass_begin(VkCommandBuffer cmd, VkImage dst, VkImageView dst_view, VkExtent2D extent, VkRect2D shown,
                        const PresentSource* src, const float clear[4]) {
    if (!g_pipeline || !dst_view) return false;
    if (src && (!src->image || !src->width || !src->height || !src->w || !src->h || !source_filters(src->format))) return false;
    // Last frame's view: its commands are done (the caller waited for the
    // presenter's fence).
    if (g_source_view) {
        vkDestroyImageView(g_device, g_source_view, nullptr);
        g_source_view = VK_NULL_HANDLE;
    }
    if (src) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = src->image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = src->format;  // the blit's conversions: an sRGB target decodes, as it did
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(g_device, &vci, nullptr, &g_source_view) != VK_SUCCESS) {
            g_source_view = VK_NULL_HANDLE;
            return false;
        }
        g_source_bound = true;
        VkDescriptorImageInfo dii{g_sampler, g_source_view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = g_set;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &dii;
        vkUpdateDescriptorSets(g_device, 1, &w, 0, nullptr);
    }
    // The renderer's writes to the display image are in earlier submissions
    // on this queue; submission order alone does not make them visible to
    // this read. The swapchain image's old contents are not wanted: from
    // UNDEFINED, after the stage the acquire is waited at.
    VkMemoryBarrier written{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    written.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    written.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkImageMemoryBarrier to_colour{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_colour.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_colour.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_colour.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_colour.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    g_encoding = g_hdr_on.load() && hdr_target(extent);
    g_shown = shown;
    g_src_region = src ? *src : PresentSource{};
    g_dst_image = dst;
    g_dst_view = dst_view;
    g_dst_extent = extent;
    to_colour.image = g_encoding ? g_hdr_image : dst;
    to_colour.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    to_colour.srcAccessMask = 0;
    to_colour.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, src ? 1 : 0, &written, 0,
                         nullptr, 1, &to_colour);
    // Every pixel is drawn when the picture fills the image: nothing to load
    // or clear. Bars, or no picture at all, are the clear.
    const bool covers = src && shown.offset.x <= 0 && shown.offset.y <= 0 &&
                        static_cast<std::int64_t>(shown.offset.x) + shown.extent.width >= extent.width &&
                        static_cast<std::int64_t>(shown.offset.y) + shown.extent.height >= extent.height;
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = g_encoding ? g_hdr_view : dst_view;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = covers ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    for (int i = 0; i < 4; ++i) att.clearValue.color.float32[i] = clear[i];
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, extent};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vkCmdBeginRendering(cmd, &ri);
    if (src) {
        // rc2: the sharpening variant only one texel a pixel (its taps are texel offsets).
        // A request older than 100 ms: FSR has stopped upscaling (a menu, a load) - plain.
        const float con = g_sharpen_con.load();
        const bool sharpen = con > 0.0f && steady_now_ns() - g_sharpen_at.load() < 100000000 && g_sharpen_pipeline && shown.extent.width == src->w && shown.extent.height == src->h;
        const float gb = g_grade_b.load(), gc = g_grade_c.load(), gh = g_grade_h.load();
        const bool grade = !sharpen && g_grade_pipeline && (gb != 1.0f || gc != 1.0f || gh != 0.0f);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sharpen ? g_sharpen_pipeline : grade ? g_grade_pipeline : g_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &g_set, 0, nullptr);
        if (sharpen) {
            const float pc[4] = {con, 0.0f, 0.0f, 0.0f};
            vkCmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(float) * 4, sizeof(pc), pc);
        } else if (grade) {
            const float rad = gh * 0.017453293f;
            const float pc[4] = {gb, gc, std::cos(rad), std::sin(rad)};
            vkCmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(float) * 4, sizeof(pc), pc);
        }
        const VkViewport vp{static_cast<float>(shown.offset.x), static_cast<float>(shown.offset.y), static_cast<float>(shown.extent.width),
                            static_cast<float>(shown.extent.height), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &shown);
        const float iw = 1.0f / static_cast<float>(src->width), ih = 1.0f / static_cast<float>(src->height);
        const float uv[4] = {static_cast<float>(src->x) * iw, static_cast<float>(src->y) * ih, static_cast<float>(src->x + src->w) * iw,
                             static_cast<float>(src->y + src->h) * ih};
        vkCmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uv), uv);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    return true;
}

void present_pass_set_hdr_ui(float ui_brightness, const PresentSource* before_ui) {
    g_ui_brightness.store(ui_brightness);
    g_preui_set = before_ui && before_ui->image;
    if (g_preui_set) g_preui = *before_ui;
}

void present_pass_end(VkCommandBuffer cmd, VkImage dst) {
    vkCmdEndRendering(cmd);
    if (g_encoding && dst == g_dst_image) {
        // HDR output: the SDR layer, finished, encoded into the swapchain image.
        VkImageMemoryBarrier b[2]{};
        b[0].sType = b[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[0].srcQueueFamilyIndex = b[0].dstQueueFamilyIndex = b[1].srcQueueFamilyIndex = b[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[0].image = g_hdr_image;
        b[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b[1].image = dst;
        b[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b[1].srcAccessMask = 0;
        b[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 2, b);
        VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        att.imageView = g_dst_view;
        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, g_dst_extent};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &att;
        vkCmdBeginRendering(cmd, &ri);
        // The UI kept apart: the display buffer (as the picture drew it) and its
        // copy from before its UI; without one this frame, the SDR layer in
        // their place and the shader told not to look.
        if (g_preui_view) {
            vkDestroyImageView(g_device, g_preui_view, nullptr);  // last frame's (the presenter's fence is behind us)
            g_preui_view = VK_NULL_HANDLE;
        }
        bool ui_apart = g_preui_set && g_source_bound && g_source_view && g_src_region.image && g_ui_brightness.load() > 0.0f;
        if (ui_apart) {
            VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vci.image = g_preui.image;
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = g_preui.format;
            vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (vkCreateImageView(g_device, &vci, nullptr, &g_preui_view) != VK_SUCCESS) {
                g_preui_view = VK_NULL_HANDLE;
                ui_apart = false;
            }
        }
        {
            VkDescriptorImageInfo ii[2] = {{g_point ? g_point : g_sampler, ui_apart ? g_source_view : g_hdr_view,
                                            ui_apart ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                           {g_point ? g_point : g_sampler, ui_apart ? g_preui_view : g_hdr_view,
                                            ui_apart ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = g_hdr_set;
            w.dstBinding = 1;
            w.descriptorCount = 2;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = ii;
            vkUpdateDescriptorSets(g_device, 1, &w, 0, nullptr);
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_hdr_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_hdr_layout, 0, 1, &g_hdr_set, 0, nullptr);
        const VkViewport vp{0.0f, 0.0f, static_cast<float>(g_dst_extent.width), static_cast<float>(g_dst_extent.height), 0.0f, 1.0f};
        const VkRect2D sc{{0, 0}, g_dst_extent};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        const float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        vkCmdPushConstants(cmd, g_hdr_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uv), uv);
        // p: paper white, peak, boost, knee; ui: apart (1/0), UI brightness (x paper white),
        // whether our overlay counts as UI; shown: the picture's rectangle; src: its region of the display buffer.
        const float pc[16] = {g_hdr_paper.load(), g_hdr_peak.load(), g_hdr_boost.load(), 0.8f,
                              ui_apart ? 1.0f : 0.0f, g_ui_brightness.load() > 0.0f ? g_ui_brightness.load() : 1.0f, g_ui_brightness.load() > 0.0f ? 1.0f : 0.0f, 0.0f,
                              static_cast<float>(g_shown.offset.x), static_cast<float>(g_shown.offset.y),
                              static_cast<float>(g_shown.extent.width), static_cast<float>(g_shown.extent.height),
                              static_cast<float>(g_src_region.x), static_cast<float>(g_src_region.y),
                              static_cast<float>(g_src_region.w), static_cast<float>(g_src_region.h)};
        vkCmdPushConstants(cmd, g_hdr_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(float) * 4, sizeof(pc), pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
        g_encoding = false;
    }
    VkImageMemoryBarrier to_present{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.image = dst;
    to_present.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    to_present.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_present.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &to_present);
}

bool present_pass_motion(VkCommandBuffer cmd, VkRect2D shown, const PresentSource& motion) {
    // Last frame's view: its commands are done (the caller waited for the presenter's fence).
    if (g_motion_view) {
        vkDestroyImageView(g_device, g_motion_view, nullptr);
        g_motion_view = VK_NULL_HANDLE;
    }
    if (!g_motion_pipeline || !g_motion_set || !g_point || !motion.image || !motion.width || !motion.height) return false;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = motion.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = motion.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(g_device, &vci, nullptr, &g_motion_view) != VK_SUCCESS) {
        g_motion_view = VK_NULL_HANDLE;
        return false;
    }
    VkDescriptorImageInfo dii{g_point, g_motion_view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = g_motion_set;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &dii;
    vkUpdateDescriptorSets(g_device, 1, &w, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_motion_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &g_motion_set, 0, nullptr);
    const VkViewport vp{static_cast<float>(shown.offset.x), static_cast<float>(shown.offset.y), static_cast<float>(shown.extent.width),
                        static_cast<float>(shown.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &shown);
    const float iw = 1.0f / static_cast<float>(motion.width), ih = 1.0f / static_cast<float>(motion.height);
    const float uv[4] = {static_cast<float>(motion.x) * iw, static_cast<float>(motion.y) * ih, static_cast<float>(motion.x + motion.w) * iw,
                         static_cast<float>(motion.y + motion.h) * ih};
    vkCmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uv), uv);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    return true;
}

bool present_pass_glass(VkCommandBuffer cmd, VkRect2D shown, const PresentSource& src, const OverlayGlass* rects, std::size_t count,
                        VkRect2D area, float display_w, float display_h) {
    if (!g_glass_pipeline || !g_source_bound || !count || !src.width || !src.height || display_w <= 0.0f || display_h <= 0.0f) return false;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_glass_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &g_set, 0, nullptr);
    const VkViewport vp{static_cast<float>(shown.offset.x), static_cast<float>(shown.offset.y), static_cast<float>(shown.extent.width),
                        static_cast<float>(shown.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    const float iw = 1.0f / static_cast<float>(src.width), ih = 1.0f / static_cast<float>(src.height);
    const float uv[4] = {static_cast<float>(src.x) * iw, static_cast<float>(src.y) * ih, static_cast<float>(src.x + src.w) * iw,
                         static_cast<float>(src.y + src.h) * ih};
    vkCmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uv), uv);
    // Overlay coordinates map onto `area` as the overlay's own draw maps them.
    const float sx = static_cast<float>(area.extent.width) / display_w, sy = static_cast<float>(area.extent.height) / display_h;
    // The source uv one framebuffer pixel spans, inside `shown`.
    const float uvx = (uv[2] - uv[0]) / static_cast<float>(shown.extent.width);
    const float uvy = (uv[3] - uv[1]) / static_cast<float>(shown.extent.height);
    for (std::size_t i = 0; i < count; ++i) {
        const OverlayGlass& g = rects[i];
        const float x0 = static_cast<float>(area.offset.x) + g.x * sx, y0 = static_cast<float>(area.offset.y) + g.y * sy;
        const float x1 = x0 + g.w * sx, y1 = y0 + g.h * sy;
        const std::int32_t l = std::max<std::int32_t>(shown.offset.x, static_cast<std::int32_t>(std::floor(x0)));
        const std::int32_t t = std::max<std::int32_t>(shown.offset.y, static_cast<std::int32_t>(std::floor(y0)));
        const std::int32_t r = std::min<std::int32_t>(shown.offset.x + static_cast<std::int32_t>(shown.extent.width), static_cast<std::int32_t>(std::ceil(x1)));
        const std::int32_t b = std::min<std::int32_t>(shown.offset.y + static_cast<std::int32_t>(shown.extent.height), static_cast<std::int32_t>(std::ceil(y1)));
        if (r <= l || b <= t) continue;
        const VkRect2D sc{{l, t}, {static_cast<std::uint32_t>(r - l), static_cast<std::uint32_t>(b - t)}};
        vkCmdSetScissor(cmd, 0, 1, &sc);
        const float blur_px = g.blur * sy;  // the blur's radius, in framebuffer pixels
        const float pc[8] = {x0, y0, x1, y1, g.radius * sy, blur_px * uvx, blur_px * uvy, g.brightness};
        vkCmdPushConstants(cmd, g_layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(float) * 4, sizeof(pc), pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    return true;
}
