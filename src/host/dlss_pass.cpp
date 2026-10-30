// Helia's Dream: DLSS at the scale pass (host/dlss_pass.h). The NGX interface
// below follows Lantern Engine's vk_dlss.cpp (Awakenchan, GPL-2.0-or-later):
// the core's C entry points and its parameter object's vtable slots.
#include "host/dlss_pass.h"

#include "host/gpu_internal.h"
#include "log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace gpu {
namespace {

// The NGX interface, as the driver's core exports it (DLSS SDK 310.x, NGX API 1.5).
using NgxResult = unsigned int;
constexpr bool failed(NgxResult r) { return (r & 0xFFF00000u) == 0xBAD00000u; }
constexpr unsigned kNgxApiVersion = 0x15;
constexpr int kFeatureSuperSampling = 1;
constexpr unsigned long long kApplicationId = 231313132;  // NVIDIA's id for unregistered applications
enum DlssFlags : int { FlagIsHdr = 1 << 0, FlagMvLowRes = 1 << 1, FlagAutoExposure = 1 << 6 };
// NVSDK_NGX_PerfQuality_Value
enum PerfQuality : int { MaxPerf = 0, Balanced = 1, MaxQuality = 2, UltraPerformance = 3, Dlaa = 5 };

struct NgxParameter;
struct NgxHandle;
struct PathListInfo {
    const wchar_t* const* path;
    unsigned int length;
};
using LogCallback = void (*)(const char* message, int level, int feature);
struct LoggingInfo {
    LogCallback callback;
    int minimum_level;
    bool disable_other_sinks;
};
struct FeatureCommonInfo {
    PathListInfo path_list;
    void* internal_data;
    LoggingInfo logging;
};
struct ImageViewInfoVk {
    VkImageView image_view;
    VkImage image;
    VkImageSubresourceRange subresource_range;
    VkFormat format;
    unsigned int width;
    unsigned int height;
};
struct BufferInfoVk {
    VkBuffer buffer;
    unsigned int size_in_bytes;
};
struct ResourceVk {
    union {
        ImageViewInfoVk image_view_info;
        BufferInfoVk buffer_info;
    } resource;
    int type;  // 0 image view, 1 buffer
    bool read_write;
};
using PFN_InitExt2 = NgxResult (*)(unsigned long long, const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr,
                                   PFN_vkGetDeviceProcAddr, unsigned, const FeatureCommonInfo*);
using PFN_GetParameters = NgxResult (*)(NgxParameter**);
using PFN_DestroyParameters = NgxResult (*)(NgxParameter*);
using PFN_CreateFeature1 = NgxResult (*)(VkDevice, VkCommandBuffer, int, NgxParameter*, NgxHandle**);
using PFN_EvaluateFeature = NgxResult (*)(VkCommandBuffer, const NgxHandle*, const NgxParameter*, void*);
using PFN_ReleaseFeature = NgxResult (*)(NgxHandle*);

// NVSDK_NGX_Parameter's Set/Get overloads, by vtable slot: MSVC lays an
// overload set out in reverse order of declaration, the Itanium ABI in order.
#ifdef _WIN32
enum ParamSlot : int { SetVoid = 0, SetInt = 3, SetUint = 4, SetFloat = 6, GetVoid = 8, GetInt = 11, GetUint = 12, GetFloat = 14 };
#else
enum ParamSlot : int { SetFloat = 1, SetUint = 3, SetInt = 4, SetVoid = 7, GetFloat = 9, GetUint = 11, GetInt = 12, GetVoid = 15 };
#endif
template <typename Fn>
Fn slot(NgxParameter* p, int s) {
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(p))[s]);
}
void set_i(NgxParameter* p, const char* n, int v) { slot<void (*)(NgxParameter*, const char*, int)>(p, SetInt)(p, n, v); }
void set_ui(NgxParameter* p, const char* n, unsigned v) { slot<void (*)(NgxParameter*, const char*, unsigned)>(p, SetUint)(p, n, v); }
void set_f(NgxParameter* p, const char* n, float v) { slot<void (*)(NgxParameter*, const char*, float)>(p, SetFloat)(p, n, v); }
void set_p(NgxParameter* p, const char* n, void* v) { slot<void (*)(NgxParameter*, const char*, void*)>(p, SetVoid)(p, n, v); }
NgxResult get_i(NgxParameter* p, const char* n, int* v) { return slot<NgxResult (*)(NgxParameter*, const char*, int*)>(p, GetInt)(p, n, v); }
NgxResult get_ui(NgxParameter* p, const char* n, unsigned* v) {
    return slot<NgxResult (*)(NgxParameter*, const char*, unsigned*)>(p, GetUint)(p, n, v);
}
NgxResult get_f(NgxParameter* p, const char* n, float* v) { return slot<NgxResult (*)(NgxParameter*, const char*, float*)>(p, GetFloat)(p, n, v); }

void ngx_log(const char* message, int, int) { host_log("dlss: NGX %s", message); }

int quality_of(int preset) {
    switch (preset) {
        case 0: return Dlaa;
        case 1: return MaxQuality;
        case 2: return Balanced;
        case 3: return MaxPerf;
        default: return UltraPerformance;
    }
}

// Lantern's "auto" render preset: the model NVIDIA's DLSS 4.5 pairs with the
// mode - M for Performance, L for Ultra Performance, K (the transformer) for
// the others. Letters count from A = 1.
unsigned preset_for(int quality) { return quality == MaxPerf ? 13u : quality == UltraPerformance ? 12u : 11u; }
void set_preset(NgxParameter* p, unsigned preset) {
    for (const char* n : {"DLSS.Hint.Render.Preset.DLAA", "DLSS.Hint.Render.Preset.Quality", "DLSS.Hint.Render.Preset.Balanced",
                          "DLSS.Hint.Render.Preset.Performance", "DLSS.Hint.Render.Preset.UltraPerformance", "DLSS.Hint.Render.Preset.UltraQuality"})
        set_ui(p, n, preset);
}

struct State {
    bool tried = false, available = false, fatal = false;
    std::string problem;
#ifdef _WIN32
    HMODULE core = nullptr;
#else
    void* core = nullptr;
#endif
    PFN_InitExt2 init = nullptr;
    PFN_GetParameters get_capability_parameters = nullptr, allocate_parameters = nullptr;
    PFN_DestroyParameters destroy_parameters = nullptr;
    PFN_CreateFeature1 create_feature = nullptr;
    PFN_EvaluateFeature evaluate_feature = nullptr;
    PFN_ReleaseFeature release_feature = nullptr;
    NgxParameter* capabilities = nullptr;
    NgxParameter* params = nullptr;
    NgxHandle* feature = nullptr;
    struct Key {
        std::uint32_t rw = 0, rh = 0, ow = 0, oh = 0;
        int quality = -1;
        bool operator==(const Key&) const = default;
    } key;
    std::uint64_t last_frame = 0;
    std::string described;
};
State g_dlss;
bool g_extensions = false;  // the device was made with NGX's extensions

struct OutImage {
    VkImage image = VK_NULL_HANDLE;
    ImageMemory memory;
    VkImageView view = VK_NULL_HANDLE;
    std::uint32_t w = 0, h = 0;
    bool general = false;
} g_out;

bool fail(const std::string& why, bool fatal) {
    if (g_dlss.problem != why) host_log("dlss: %s", why.c_str());
    g_dlss.problem = why;
    g_dlss.fatal = g_dlss.fatal || fatal;
    return false;
}

template <typename Fn>
bool load(Fn& fn, const char* name) {
#ifdef _WIN32
    fn = reinterpret_cast<Fn>(GetProcAddress(g_dlss.core, name));
#else
    fn = reinterpret_cast<Fn>(dlsym(g_dlss.core, name));
#endif
    return fn != nullptr;
}

bool load_core() {
#ifdef _WIN32
    // The driver records its NGX core here (nvsdk_ngx_loader.h does the same).
    wchar_t dir[MAX_PATH];
    DWORD size = sizeof(dir);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", L"FullPath", RRF_RT_REG_SZ, nullptr, dir, &size) ==
        ERROR_SUCCESS)
        g_dlss.core = LoadLibraryW((std::wstring(dir) + L"\\_nvngx.dll").c_str());
    // OptiScaler, dropped next to the executable as nvngx.dll, stands in for it on any GPU.
    if (!g_dlss.core) g_dlss.core = LoadLibraryW(L"nvngx.dll");
#else
    g_dlss.core = dlopen("libnvidia-ngx.so.1", RTLD_NOW);
#endif
    if (!g_dlss.core) return fail("no NVIDIA NGX core (DLSS needs an NVIDIA RTX GPU and its driver): FSR 3.1 upscales instead", true);
    if (!load(g_dlss.init, "NVSDK_NGX_VULKAN_Init_Ext2") || !load(g_dlss.get_capability_parameters, "NVSDK_NGX_VULKAN_GetCapabilityParameters") ||
        !load(g_dlss.allocate_parameters, "NVSDK_NGX_VULKAN_AllocateParameters") ||
        !load(g_dlss.destroy_parameters, "NVSDK_NGX_VULKAN_DestroyParameters") ||
        !load(g_dlss.create_feature, "NVSDK_NGX_VULKAN_CreateFeature1") || !load(g_dlss.evaluate_feature, "NVSDK_NGX_VULKAN_EvaluateFeature") ||
        !load(g_dlss.release_feature, "NVSDK_NGX_VULKAN_ReleaseFeature"))
        return fail("the NGX core lacks the Vulkan entry points", true);
    return true;
}

// Where nvngx_dlss is looked for: HELIAS_DLSS_DIR, then beside the executable.
std::vector<std::wstring> search_paths() {
    std::vector<std::wstring> paths;
    if (const char* e = std::getenv("HELIAS_DLSS_DIR"); e && *e) paths.push_back(std::filesystem::path(e).wstring());
#ifdef _WIN32
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) paths.push_back(std::filesystem::path(exe).parent_path().wstring());
#else
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) paths.push_back(exe.parent_path().wstring());
#endif
    return paths;
}

// The vtable slots are right when values set through them read back unchanged.
bool parameter_abi_ok(NgxParameter* p) {
    set_i(p, "helias.Probe.I", 0x5A5A);
    set_ui(p, "helias.Probe.UI", 77u);
    set_f(p, "helias.Probe.F", 1.25f);
    int i = 0;
    unsigned ui = 0;
    float f = 0.0f;
    return !failed(get_i(p, "helias.Probe.I", &i)) && i == 0x5A5A && !failed(get_ui(p, "helias.Probe.UI", &ui)) && ui == 77u &&
           !failed(get_f(p, "helias.Probe.F", &f)) && f == 1.25f;
}

void initialize() {
    g_dlss.tried = true;
    if (!g_extensions) {
        fail("the device lacks VK_NVX_binary_import / VK_NVX_image_view_handle (DLSS needs an NVIDIA RTX GPU; choosing DLSS takes effect at the next start)",
             true);
        return;
    }
    if (!load_core()) return;
    std::error_code ec;
    const auto data_dir = std::filesystem::temp_directory_path(ec) / "helias-dream-ngx";
    std::filesystem::create_directories(data_dir, ec);
    const std::wstring data_path = data_dir.wstring();
    const auto paths = search_paths();
    std::vector<const wchar_t*> path_ptrs;
    for (const auto& p : paths) path_ptrs.push_back(p.c_str());
    const FeatureCommonInfo info{{path_ptrs.data(), static_cast<unsigned>(path_ptrs.size())}, nullptr,
                                 {&ngx_log, std::getenv("HELIAS_DLSS_LOG") ? 2 : 0, false}};
    const NgxResult r = g_dlss.init(kApplicationId, data_path.c_str(), g.instance, g.phys, g.device, &vkGetInstanceProcAddr, &vkGetDeviceProcAddr,
                                    kNgxApiVersion, &info);
    if (failed(r)) {
        fail("NGX would not start (" + std::to_string(r) + ")", true);
        return;
    }
    if (failed(g_dlss.get_capability_parameters(&g_dlss.capabilities)) || !g_dlss.capabilities) {
        fail("NGX capability query failed", true);
        return;
    }
    if (!parameter_abi_ok(g_dlss.capabilities)) {
        fail("NGX parameter interface mismatch", true);
        return;
    }
    int available = 0, needs_driver = 0;
    get_i(g_dlss.capabilities, "SuperSampling.NeedsUpdatedDriver", &needs_driver);
    if (failed(get_i(g_dlss.capabilities, "SuperSampling.Available", &available)) || !available) {
        fail(needs_driver ? "DLSS needs a newer NVIDIA driver"
                          : "DLSS unavailable: put NVIDIA's nvngx_dlss (libnvidia-ngx-dlss.so / nvngx_dlss.dll) next to heliasdream or in HELIAS_DLSS_DIR",
             true);
        return;
    }
    if (failed(g_dlss.allocate_parameters(&g_dlss.params)) || !g_dlss.params) {
        fail("NGX parameter allocation failed", true);
        return;
    }
    g_dlss.available = true;
    host_log("dlss: DLSS Super Resolution available (NGX core loaded)");
}

bool make_output(std::uint32_t w, std::uint32_t h) {
    if (g_out.image && g_out.w == w && g_out.h == h) return true;
    if (g_out.image) {
        if (g_out.view) defer_destroy_private_view(g_out.view);
        defer_destroy_image(g_out.image, g_out.memory);
        g_out = OutImage{};
    }
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateImage(g.device, &ci, nullptr, &g_out.image) != VK_SUCCESS) {
        g_out = OutImage{};
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(g.device, g_out.image, &req);
    if (!image_memory_alloc(req, g_out.memory)) {
        vkDestroyImage(g.device, g_out.image, nullptr);
        g_out = OutImage{};
        return false;
    }
    vkBindImageMemory(g.device, g_out.image, g_out.memory.memory, g_out.memory.offset);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = g_out.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ci.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(g.device, &vci, nullptr, &g_out.view) != VK_SUCCESS) g_out.view = VK_NULL_HANDLE;
    g_out.w = w;
    g_out.h = h;
    return g_out.view != VK_NULL_HANDLE;
}

ResourceVk resource(VkImage image, VkImageView view, VkFormat format, std::uint32_t w, std::uint32_t h, bool rw) {
    ResourceVk r{};
    r.resource.image_view_info = {view, image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, format, w, h};
    r.type = 0;
    r.read_write = rw;
    return r;
}

}  // namespace

bool dlss_device_ready() { return g_extensions; }

void dlss_device_extensions(std::vector<const char*>& exts) {
    std::uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(g.phys, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> props(n);
    vkEnumerateDeviceExtensionProperties(g.phys, nullptr, &n, props.data());
    const auto has = [&](const char* name) {
        for (const VkExtensionProperties& p : props)
            if (!std::strcmp(p.extensionName, name)) return true;
        return false;
    };
    if (has("VK_NVX_binary_import") && has("VK_NVX_image_view_handle")) {
        exts.push_back("VK_NVX_binary_import");
        exts.push_back("VK_NVX_image_view_handle");
        g_extensions = true;
        host_log("dlss: the device takes NGX's extensions (DLSS chosen)");
    } else {
        host_log("dlss: the GPU has no NGX extensions (DLSS needs an NVIDIA RTX GPU): FSR 3.1 will stand in");
    }
}

bool dlss_record_locked(VkCommandBuffer cmd, const UpscalerInputs& in, std::uint32_t output_w, std::uint32_t output_h, const Fsr4Params& p,
                        int preset, VkImage* out, bool* fatal, std::string* why) {
    *fatal = false;
    if (!g_dlss.tried) initialize();
    if (!g_dlss.available) {
        *fatal = g_dlss.fatal;
        *why = g_dlss.problem;
        return false;
    }
    if (!make_output(output_w, output_h)) {
        *why = "could not make its output image";
        return false;
    }
    if (!g_out.general) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = g_out.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        g_out.general = true;
    }
    // A scene at the output's size is DLAA, whatever the preset.
    const bool native = in.render_w == output_w && in.render_h == output_h;
    const State::Key want{in.render_w, in.render_h, output_w, output_h, native ? int(Dlaa) : quality_of(preset)};
    NgxParameter* params = g_dlss.params;
    if (!g_dlss.feature || !(want == g_dlss.key)) {
        if (g_dlss.feature) {
            // The submissions that used it must be done before it goes.
            if (g_dlss.last_frame) wait_submission_locked(g_dlss.last_frame);
            g_dlss.release_feature(g_dlss.feature);
            g_dlss.feature = nullptr;
        }
        set_ui(params, "CreationNodeMask", 1);
        set_ui(params, "VisibilityNodeMask", 1);
        set_ui(params, "Width", want.rw);
        set_ui(params, "Height", want.rh);
        set_ui(params, "OutWidth", want.ow);
        set_ui(params, "OutHeight", want.oh);
        set_i(params, "PerfQualityValue", want.quality);
        // The game's scene colour is linear HDR (RGBA16F); motion vectors at the render size, jitter removed.
        set_i(params, "DLSS.Feature.Create.Flags", FlagMvLowRes | FlagIsHdr | FlagAutoExposure);
        set_i(params, "DLSS.Enable.Output.Subrects", 0);
        // The model chosen, not left to the DLL's default: the mode's (preset_for); a preset the DLL refuses gets its default.
        const unsigned preset = preset_for(want.quality);
        set_preset(params, preset);
        NgxResult r = g_dlss.create_feature(g.device, cmd, kFeatureSuperSampling, params, &g_dlss.feature);
        if (failed(r) || !g_dlss.feature) {
            host_log("dlss: preset %c refused (%u); the DLL's default", static_cast<char>('A' + preset - 1), r);
            set_preset(params, 0);
            g_dlss.feature = nullptr;
            r = g_dlss.create_feature(g.device, cmd, kFeatureSuperSampling, params, &g_dlss.feature);
        }
        if (failed(r) || !g_dlss.feature) {
            g_dlss.feature = nullptr;
            *why = "DLSS: the feature could not be made (" + std::to_string(r) + ")";
            return false;
        }
        g_dlss.key = want;
        char d[160];
        std::snprintf(d, sizeof(d), "%ux%u -> %ux%u, quality mode %d", want.rw, want.rh, want.ow, want.oh, want.quality);
        g_dlss.described = d;
        host_log("dlss: %s", d);
    }
    const bool direct = p.direct_image && p.direct_view;
    const VkImageView cv = pass_view_locked(in.color.image, in.color.format, VK_IMAGE_ASPECT_COLOR_BIT);
    const VkImageView dv = pass_view_locked(in.depth_copy.image, in.depth_copy.format, VK_IMAGE_ASPECT_COLOR_BIT);
    const VkImageView mv = pass_view_locked(in.motion.image, in.motion.format, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!cv || !dv || !mv) {
        *why = "DLSS: could not make the inputs' views";
        return false;
    }
    ResourceVk color = resource(in.color.image, cv, in.color.format, in.color.width, in.color.height, false);
    ResourceVk depth = resource(in.depth_copy.image, dv, in.depth_copy.format, in.depth_copy.width, in.depth_copy.height, false);
    ResourceVk motion = resource(in.motion.image, mv, in.motion.format, in.motion.width, in.motion.height, false);
    ResourceVk output = direct ? resource(p.direct_image, p.direct_view, VK_FORMAT_R16G16B16A16_SFLOAT, output_w, output_h, true)
                               : resource(g_out.image, g_out.view, VK_FORMAT_R16G16B16A16_SFLOAT, g_out.w, g_out.h, true);
    set_p(params, "Color", &color);
    set_p(params, "Depth", &depth);
    set_p(params, "MotionVectors", &motion);
    set_p(params, "Output", &output);
    // The reactive mask as DLSS's bias towards the current colour (1 trusts this frame alone where blended effects changed it).
    ResourceVk bias{};
    const VkImageView rv = in.reactive.image ? pass_view_locked(in.reactive.image, in.reactive.format, VK_IMAGE_ASPECT_COLOR_BIT) : VK_NULL_HANDLE;
    if (rv) bias = resource(in.reactive.image, rv, in.reactive.format, in.reactive.width, in.reactive.height, false);
    set_p(params, "DLSS.Input.Bias.Current.Color.Mask", rv ? &bias : nullptr);
    set_ui(params, "DLSS.Input.Bias.Current.Color.Subrect.Base.X", 0);
    set_ui(params, "DLSS.Input.Bias.Current.Color.Subrect.Base.Y", 0);
    set_f(params, "Jitter.Offset.X", p.jitter_x);
    set_f(params, "Jitter.Offset.Y", p.jitter_y);
    set_f(params, "Sharpness", 0.0f);
    set_i(params, "Reset", p.reset ? 1 : 0);
    set_f(params, "MV.Scale.X", 1.0f);
    set_f(params, "MV.Scale.Y", 1.0f);
    set_ui(params, "DLSS.Render.Subrect.Dimensions.Width", in.render_w);
    set_ui(params, "DLSS.Render.Subrect.Dimensions.Height", in.render_h);
    set_f(params, "FrameTimeDeltaInMsec", in.frame_ms > 0.0f ? in.frame_ms : 16.7f);
    const NgxResult r = g_dlss.evaluate_feature(cmd, g_dlss.feature, params, nullptr);
    if (failed(r)) {
        *why = "DLSS: evaluation failed (" + std::to_string(r) + ")";
        return false;
    }
    g_dlss.last_frame = g.flushes + 1;
    *out = direct ? p.direct_image : g_out.image;
    return true;
}

std::string dlss_describe_locked() { return g_dlss.described; }

}  // namespace gpu
