// heliasdream-vk Phase 3: FSR 3.1 at the game's scale pass (host/fsr3_pass.h).
//
// Per frame, at the scale pass (ccbf44a6) with FSR 3.1 (or FSR 4.1.1, until
// Phase 4) chosen and the scene split:
//   - the inputs host/temporal_inputs.cpp made a moment ago: the HDR scene
//     colour, the depth copy (R32F), the motion vectors, the reactive mask,
//     the jitter, the camera cut;
//   - a context for exactly this render and output size (a new one, and a
//     history reset, when either changes; the old one destroyed once the GPU
//     is past its last frame);
//   - the history reset on a camera cut, a new context, or a gap of frames;
//   - sharpening from the setting (FSR's RCAS), exposure FSR's own;
//   - FSR 3.1.5 records into the renderer's command buffer, then its output
//     (RGBA16F at the output size) is copied - or blitted, for another
//     format - into the pass's target, and the game's draw is skipped;
//   - with Show Upscaler GPU Time on, timestamps around the dispatch.
// Anything missing: the game's draw runs (Phase 1's bilinear copy) and the
// reason is logged when it changes.
#include "host/fsr3_pass.h"

#if HELIAS_HAVE_FSR_VULKAN

#include "engine/live_resolution.h"
#include "engine/scene_split.h"
#include "hle/modules.h"
#include "host/fsr3_upscaler.h"
#include "host/fsr4_pass.h"
#include "host/fsr4v07_pass.h"
#include "host/dlss_pass.h"
#include "host/fsr_dump.h"
#include "host/pass_timer.h"
#include "host/gpu_internal.h"
#include "host/options.h"
#include "host/settings.h"
#include "host/temporal/fsr3_policy.h"
#include "host/temporal_inputs.h"
#include "host/upscaler_presets.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace gpu {
namespace {

enum class Fallback : int {
    None = 0,
    Disabled,       // HELIAS_FSR3=0
    NoInputs,       // no scene colour, depth or motion this frame
    Sizes,          // a ratio FSR 3.1 does not take
    Target,         // the target is not a plain 2D colour image of the output size
    Format,         // the target's format can neither be copied nor blitted to
    Context,        // the context could not be made for these sizes
    Dispatch,       // the dispatch failed
    kCount
};
const char* fallback_name(Fallback f) {
    switch (f) {
    case Fallback::None: return "none";
    case Fallback::Disabled: return "HELIAS_FSR3=0";
    case Fallback::NoInputs: return "no scene inputs this frame (colour, depth copy or motion vectors)";
    case Fallback::Sizes: return "the scene/output ratio is outside FSR 3.1's 1x..3x";
    case Fallback::Target: return "the scale pass's target is not a plain colour image of the output size";
    case Fallback::Format: return "the target's format takes neither a copy nor a blit from RGBA16F";
    case Fallback::Context: return "the FSR 3.1 context could not be made";
    case Fallback::Dispatch: return "the FSR 3.1 dispatch failed";
    default: return "?";
    }
}

struct Grave {
    std::unique_ptr<Fsr3Upscaler> fsr;
    std::uint64_t last = 0;
};

std::unique_ptr<Fsr3Upscaler> g_fsr;
std::vector<Grave> g_graves;
temporal::Fsr3Sizes g_failed_sizes;  // the context failed for these: not retried until they change
temporal::Fsr3History g_history;
std::uint64_t g_last_frame = ~0ull;  // the flip last dispatched (a second scale pass in it copies again)
Fallback g_last_fallback = Fallback::kCount;
bool g_fsr4_off = false;  // FSR 4.1.1 cannot run in this session (assets, device): FSR 3.1
bool g_fsr4v07_off = false;  // nor FSR 4 (INT8 v07, host/fsr4v07_pass.h)
bool g_dlss_off = false;     // nor DLSS (host/dlss_pass.h)
temporal::Fsr3History g_history4;
std::uint64_t g_last_frame4 = ~0ull;
VkImage g_last_out4 = VK_NULL_HANDLE;
VkImage g_last_direct = VK_NULL_HANDLE;  // test4: the target FSR 3.1 last wrote directly (a repeat copies from it)
std::string g_fsr4_described;

struct Stats {
    std::uint64_t dispatches = 0, fsr4 = 0, copies = 0, blits = 0, repeats = 0, resets = 0, contexts = 0, direct = 0;
    std::uint64_t fallbacks[static_cast<int>(Fallback::kCount)] = {};
};
Stats g_window;

// ---- the timer: timestamps around the dispatch, read once the submission retired ----
// ---- the copy into the target ----
// Whether `format` (optimal tiling) can be a blit's destination, and RGBA16F its source.
bool blit_ok(VkFormat format) {
    static std::map<VkFormat, bool> known;
    auto it = known.find(format);
    if (it != known.end()) return it->second;
    VkFormatProperties dst{}, src{};
    vkGetPhysicalDeviceFormatProperties(g.phys, format, &dst);
    vkGetPhysicalDeviceFormatProperties(g.phys, VK_FORMAT_R16G16B16A16_SFLOAT, &src);
    const bool ok = (dst.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) && (src.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
    known[format] = ok;
    return ok;
}

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags src_access, VkPipelineStageFlags dst, VkAccessFlags dst_access) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = src_access;
    mb.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

bool copy_output(VkCommandBuffer cmd, VkImage src, VkFormat src_format, std::uint32_t w, std::uint32_t h, RtImage& target,
                 const temporal::OutputRect& at) {
    barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    if (target.format == src_format) {
        VkImageCopy c{};
        c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.dstSubresource = c.srcSubresource;
        c.dstOffset = {static_cast<std::int32_t>(at.x), static_cast<std::int32_t>(at.y), 0};
        c.extent = {w, h, 1};
        vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_GENERAL, target.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        ++g_window.copies;
    } else {
        VkImageBlit b{};
        b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        b.dstSubresource = b.srcSubresource;
        b.srcOffsets[1] = {static_cast<std::int32_t>(w), static_cast<std::int32_t>(h), 1};
        b.dstOffsets[0] = {static_cast<std::int32_t>(at.x), static_cast<std::int32_t>(at.y), 0};
        b.dstOffsets[1] = {static_cast<std::int32_t>(at.x + w), static_cast<std::int32_t>(at.y + h), 1};
        vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_GENERAL, target.image, VK_IMAGE_LAYOUT_GENERAL, 1, &b, VK_FILTER_NEAREST);
        ++g_window.blits;
    }
    barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    return true;
}

bool fall_back(Fallback why) {
    ++g_window.fallbacks[static_cast<int>(why)];
    if (why != g_last_fallback) {
        g_last_fallback = why;
        host_log("fsr3: the scale pass is drawn bilinear this frame: %s (said once until it changes)", fallback_name(why));
    }
    return false;
}

void dig_locked() {
    for (auto it = g_graves.begin(); it != g_graves.end();) {
        if (it->last <= g.completed_submits) {
            it->fsr->destroy();
            it = g_graves.erase(it);
        } else {
            it->fsr->retire(g.completed_submits);
            ++it;
        }
    }
}

int env_sign(const char* name, int axis) {
    // HELIAS_FSR_JITTER_SIGN=x, y or xy: flip that axis' jitter for FSR.
    const char* e = std::getenv(name);
    if (!e) return 1;
    return std::strchr(e, axis == 0 ? 'x' : 'y') ? -1 : 1;
}

// ---- the stage dump (host/fsr_dump.h, HELIAS_FSR_DUMP=1) ----
std::uint64_t g_upscaled = 0;  // frames upscaled this run (the dump's automatic one)

// The inputs and the target as it is before the copy; true when this frame dumps.
bool dump_begin_locked(const UpscalerInputs& in, const RtImage& target, const temporal::OutputRect& out, const char* which, bool reset) {
    if (!fsr_dump_wanted_locked(in.frame, g_upscaled)) return false;
    char line[400];
    std::snprintf(line, sizeof(line),
                  "%s at flip %llu: scene %ux%u -> output %ux%u at %u,%u in the scale pass's %ux%u target (format %d); jitter %.3f, %.3f "
                  "(phase %u of %u); history reset %s; cut %s; near %g far %g fov %g",
                  which, static_cast<unsigned long long>(in.frame), in.render_w, in.render_h, out.w, out.h, out.x, out.y, target.width, target.height,
                  static_cast<int>(target.format), static_cast<double>(in.jitter_x), static_cast<double>(in.jitter_y), in.jitter_phase,
                  in.jitter_phases, reset ? "yes" : "no", temporal::cut_name(in.cut), static_cast<double>(in.near_plane),
                  static_cast<double>(in.far_plane), static_cast<double>(in.fov_y));
    fsr_dump_note_locked(line);
    fsr_dump_add_locked("1-scene-colour", in.color.image, in.color.format, in.color.width, in.color.height, temporal::DumpKind::Color);
    fsr_dump_add_locked("2-depth-copy", in.depth_copy.image, in.depth_copy.format, in.depth_copy.width, in.depth_copy.height, temporal::DumpKind::Depth);
    fsr_dump_add_locked("3-motion", in.motion.image, in.motion.format, in.motion.width, in.motion.height, temporal::DumpKind::Motion);
    fsr_dump_add_locked("4-reactive", in.reactive.image, in.reactive.format, in.reactive.width, in.reactive.height, temporal::DumpKind::Mask);
    fsr_dump_add_locked("5-opaque-colour", in.opaque_color.image, in.opaque_color.format, in.opaque_color.width, in.opaque_color.height,
                        temporal::DumpKind::Color);
    fsr_dump_add_locked("7-target-before", target.image, target.format, target.width, target.height, temporal::DumpKind::Color);
    return true;
}

// The upscaler's own output and the target after the copy; then the files.
void dump_end_locked(bool on, std::uint64_t flip, VkImage output, VkFormat format, std::uint32_t w, std::uint32_t h, const RtImage& target) {
    if (!on) return;
    fsr_dump_add_locked("6-upscaler-output", output, format, w, h, temporal::DumpKind::Color);
    fsr_dump_add_locked("8-target-after", target.image, target.format, target.width, target.height, temporal::DumpKind::Color);
    fsr_dump_finish_locked(flip);
}

}  // namespace

temporal::OutputRect fsr3_output_rect_locked(const RtImage* target, const float* vport) {
    if (!target) return {};
    static std::uint64_t serial = ~0ull;
    static std::uint32_t active_w = 0, active_h = 0;
    if (host_opt_serial() != serial) {
        serial = host_opt_serial();
        const HostSettings hs = host_settings();
        active_w = hs.res_width > 0 ? static_cast<std::uint32_t>(hs.res_width) : 0;
        active_h = hs.res_height > 0 ? static_cast<std::uint32_t>(hs.res_height) : 0;
    }
    float x = 0, y = 0, w = 0, h = 0;
    if (vport && vport[0] != 0.0f) {
        x = vport[1] - vport[0];
        w = 2.0f * vport[0];
        y = vport[3] - vport[2];
        h = 2.0f * vport[2];
    }
    temporal::OutputFrom from{};
    const temporal::OutputRect r = temporal::fsr3_output_rect(target->width, target->height, x, y, w, h, active_w, active_h, &from);
    static temporal::OutputRect said{};
    static std::uint32_t said_tw = 0, said_th = 0;
    if (r != said || target->width != said_tw || target->height != said_th) {
        said = r;
        said_tw = target->width;
        said_th = target->height;
        host_log("fsr3: output %ux%u at %u,%u in the scale pass's %ux%u target (from %s; Resolution %ux%u)", r.w, r.h, r.x, r.y, target->width,
                 target->height, temporal::output_from_name(from), active_w, active_h);
        if (active_w && (r.w != active_w || r.h != active_h))
            host_log("fsr3: warning: the upscaler's output %ux%u is not the Resolution setting's %ux%u", r.w, r.h, active_w, active_h);
        if (from == temporal::OutputFrom::ViewportDiffers)
            host_log("fsr3: warning: the scale pass's viewport is %.0fx%.0f at %.0f,%.0f, not the Resolution setting's %ux%u", w, h < 0 ? -h : h, x,
                     h < 0 ? y + h : y, active_w, active_h);
    }
    return r;
}

bool fsr3_scale_pass_locked(RtImage* target, const temporal::OutputRect& out, const UpscalerImage* color) {
    static const bool disabled = [] {
        const char* e = std::getenv("HELIAS_FSR3");
        return e && e[0] == '0';
    }();
    dig_locked();
    if (g_fsr) g_fsr->retire(g.completed_submits);
    pass_timer_collect_locked();
    if (disabled) return fall_back(Fallback::Disabled);
    // The settings: a copy once they moved.
    static std::uint64_t serial = ~0ull;
    static HostSettings hs;
    if (host_opt_serial() != serial) {
        serial = host_opt_serial();
        hs = host_settings();
    }
    const UpscalerInputs& in0 = temporal_inputs_locked();
    static UpscalerInputs in_post;  // test10: the inputs with the post chain's colour
    if (color) {
        in_post = in0;
        in_post.color = *color;
    }
    const UpscalerInputs& in = color ? in_post : in0;
    if (!target || target->depth || target->layers != 1) return fall_back(Fallback::Target);
    if (!in.valid || !in.color.image || !in.depth_copy.image || !in.motion.image || in.frame != hle_video_flip_count())
        return fall_back(Fallback::NoInputs);
    if (!out.w || !out.h || out.x + out.w > target->width || out.y + out.h > target->height) return fall_back(Fallback::Sizes);
    const temporal::Fsr3Sizes sizes{in.render_w, in.render_h, out.w, out.h};
    if (!temporal::fsr3_sizes_ok(sizes) || in.color.width < in.render_w || in.color.height < in.render_h) return fall_back(Fallback::Sizes);
    if (target->format != VK_FORMAT_R16G16B16A16_SFLOAT && !blit_ok(target->format)) return fall_back(Fallback::Format);
    // FSR 4.1.1 (host/fsr4_pass.h), replayed from the user's DLL, or FSR 4
    // (host/fsr4v07_pass.h), AMD's INT8 model; FSR 3.1 below when they cannot.
    // DLSS (host/dlss_pass.h) takes the same path.
    const bool v07 = hs.upscaler == upscaler::kFsr4, dlss = hs.upscaler == upscaler::kDlss;
    const char* const name4 = dlss ? "DLSS" : v07 ? "FSR 4" : "FSR 4.1.1";
    if ((hs.upscaler == upscaler::kFsr411 && !g_fsr4_off) || (v07 && !g_fsr4v07_off) || (dlss && !g_dlss_off)) {
        begin_recording_locked();
        render_end_pass_locked();
        VkCommandBuffer cmd4 = g_cmd();
        if (in.frame == g_last_frame4 && g_last_out4) {
            ++g_window.repeats;
            if (g_last_out4 != target->image) copy_output(cmd4, g_last_out4, VK_FORMAT_R16G16B16A16_SFLOAT, out.w, out.h, *target, out);
            return true;
        }
        barrier(cmd4, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        const temporal::Fsr3Reset reset4 = temporal::fsr3_reset(g_history4, in.frame, in.reset, false);
        static const int sx = env_sign("HELIAS_FSR_JITTER_SIGN", 0), sy = env_sign("HELIAS_FSR_JITTER_SIGN", 1);
        const temporal::Fsr3Conventions c4 = temporal::fsr3_conventions(in.jitter_x, in.jitter_y, sx, sy);
        const temporal::Fsr3Sharpen sh4 = temporal::fsr3_sharpen(hs.upscale_sharpness);
        Fsr4Params p4;
        p4.jitter_x = c4.jitter_x;
        p4.jitter_y = c4.jitter_y;
        p4.reset = reset4 != temporal::Fsr3Reset::None;
        p4.sharpen = sh4.enable;
        p4.sharpness = sh4.sharpness;
        p4.ultra_performance = hs.upscale_quality >= 4;
        const bool dump4 = dump_begin_locked(in, *target, out, name4, p4.reset);
        // test5: as FSR 3.1 since test4, straight into the target when it took STORAGE (test4: copy 0.26 ms).
        const bool direct4 = target->view && temporal::direct_output_ok(target->upscaler_storage, static_cast<int>(target->format), target->width,
                                                                        target->height, out, out.w, out.h, dump4);
        if (direct4) {
            p4.direct_image = target->image;
            p4.direct_view = target->view;
        }
        const std::int32_t slot4 = pass_timer_begin_locked(cmd4, TimedPass::Upscale);
        VkImage out4 = VK_NULL_HANDLE;
        bool fatal = false;
        std::string why4;
        const bool ok4 = dlss  ? dlss_record_locked(cmd4, in, out.w, out.h, p4, hs.upscale_quality, &out4, &fatal, &why4)
                         : v07 ? fsr4v07_record_locked(cmd4, in, out.w, out.h, p4, &out4, &fatal, &why4)
                               : fsr4_record_locked(cmd4, in, out.w, out.h, p4, &out4, &fatal, &why4);
        pass_timer_end_locked(cmd4, slot4);
        if (ok4) {
            if (p4.reset) ++g_window.resets;
            if (direct4) {
                barrier(cmd4, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
                ++g_window.direct;
                static bool said = false;
                if (!said) {
                    said = true;
                    host_log("fsr4: %s writes the scale pass's %ux%u target directly (no copy; HELIAS_FSR_DIRECT_TARGET=0: the copy)",
                             name4, target->width, target->height);
                }
            } else {
                const std::int32_t copy4 = pass_timer_begin_locked(cmd4, TimedPass::Copy);
                copy_output(cmd4, out4, VK_FORMAT_R16G16B16A16_SFLOAT, out.w, out.h, *target, out);
                pass_timer_end_locked(cmd4, copy4);
            }
            ++g_upscaled;
            dump_end_locked(dump4, in.frame, out4, VK_FORMAT_R16G16B16A16_SFLOAT, out.w, out.h, *target);
            g_last_frame4 = in.frame;
            g_last_out4 = out4;
            ++g_window.dispatches;
            ++g_window.fsr4;
            pass_timer_upscaler_name(name4);
            if (const std::string d = dlss ? dlss_describe_locked() : v07 ? fsr4v07_describe_locked() : fsr4_describe_locked(); d != g_fsr4_described) {
                g_fsr4_described = d;
                host_log("fsr4: %s, %s (scene %ux%u -> %ux%u)", dlss ? "DLSS" : v07 ? "FSR 4 (INT8 v07)" : "FSR 4.1.1 replay", d.c_str(), in.render_w, in.render_h, out.w, out.h);
            }
            g_last_fallback = Fallback::None;
            // test6: the 3000-frame summary for FSR 4.1.1 too (test5 had none,
            // so its resets and direct writes went uncounted in the log).
            if (g_window.dispatches >= 3000) {
                host_log("%s", fsr3_report_locked().c_str());
                g_window = Stats{};
            }
            return true;
        }
        if (dump4) fsr_dump_finish_locked(in.frame);  // the inputs at least
        g_history4 = {};
        if (fatal) {
            if (dlss) g_dlss_off = true;
            else if (v07) g_fsr4v07_off = true;
            else {
                g_fsr4_off = true;
                host_opt_fsr4_unavailable();  // anti_alias Auto: the game's AA back on
            }
            host_log("fsr4: %s: FSR 3.1 upscales instead", why4.c_str());
        } else {
            static int said = 0;
            if (said++ < 8) host_log("fsr4: %s: FSR 3.1 this frame", why4.c_str());
        }
    }
    // The context.
    if (!g_fsr || !temporal::fsr3_context_fits({g_fsr->render_w(), g_fsr->render_h(), g_fsr->output_w(), g_fsr->output_h()}, sizes)) {
        if (sizes == g_failed_sizes) return fall_back(Fallback::Context);
        if (g_fsr) {
            g_graves.push_back({std::move(g_fsr), 0});
            g_graves.back().last = g_graves.back().fsr->last_frame_id();
        }
        const auto t0 = std::chrono::steady_clock::now();
        auto made = std::make_unique<Fsr3Upscaler>();
        std::string why;
        if (!made->create(g.phys, g.device, sizes.render_w, sizes.render_h, sizes.output_w, sizes.output_h, &why)) {
            g_failed_sizes = sizes;
            host_log("fsr3: %s (scene %ux%u, output %ux%u): the bilinear copy instead", why.c_str(), sizes.render_w, sizes.render_h, sizes.output_w,
                     sizes.output_h);
            return fall_back(Fallback::Context);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        host_log("fsr3: FSR 3.1.5 context for scene %ux%u -> output %ux%u (%.2fx), %.1f MiB, made in %.0f ms; target format %d (%s)",
                 sizes.render_w, sizes.render_h, sizes.output_w, sizes.output_h, static_cast<double>(sizes.output_w) / sizes.render_w,
                 static_cast<double>(made->memory_bytes()) / (1024.0 * 1024.0), ms, static_cast<int>(target->format),
                 target->format == VK_FORMAT_R16G16B16A16_SFLOAT ? "copied" : "blitted");
        g_fsr = std::move(made);
        g_failed_sizes = {};
        g_history = {};
        ++g_window.contexts;
    }
    begin_recording_locked();
    render_end_pass_locked();
    VkCommandBuffer cmd = g_cmd();
    if (in.frame == g_last_frame) {
        // A second scale pass in the same frame: the same picture again.
        ++g_window.repeats;
        if (g_last_direct) {
            // FSR wrote the first target itself: that picture into this one.
            if (g_last_direct != target->image) {
                barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
                VkImageCopy c{};
                c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                c.dstSubresource = c.srcSubresource;
                c.dstOffset = {static_cast<std::int32_t>(out.x), static_cast<std::int32_t>(out.y), 0};
                c.extent = {g_fsr->output_w(), g_fsr->output_h(), 1};
                if (target->format == VK_FORMAT_R16G16B16A16_SFLOAT) vkCmdCopyImage(cmd, g_last_direct, VK_IMAGE_LAYOUT_GENERAL, target->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
                barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
            }
            return true;
        }
        copy_output(cmd, g_fsr->output(), g_fsr->output_format(), g_fsr->output_w(), g_fsr->output_h(), *target, out);
        return true;
    }
    const temporal::Fsr3Reset reset = temporal::fsr3_reset(g_history, in.frame, in.reset, false);
    if (reset != temporal::Fsr3Reset::None) {
        ++g_window.resets;
        static temporal::Fsr3Reset said = temporal::Fsr3Reset::None;
        static std::uint64_t said_at = 0, since = 0;
        // Each kind once, then at most every 600 flips (cuts repeat during
        // loads), with how many there were in between: test1's lines every
        // 601 flips were this throttle over a reset every frame.
        if (reset != said || in.frame > said_at + 600) {
            host_log("fsr3: history reset at flip %llu: %s (%llu resets since the last line)", static_cast<unsigned long long>(in.frame),
                     temporal::fsr3_reset_name(reset), static_cast<unsigned long long>(since));
            said = reset;
            said_at = in.frame;
            since = 0;
        } else {
            ++since;
        }
    }
    Fsr3Frame fr;
    fr.cmd = cmd;
    fr.color = in.color;
    fr.depth = in.depth_copy;
    fr.motion = in.motion;
    if (hs.upscale_reactive) fr.reactive = in.reactive;
    static const int sign_x = env_sign("HELIAS_FSR_JITTER_SIGN", 0), sign_y = env_sign("HELIAS_FSR_JITTER_SIGN", 1);
    const temporal::Fsr3Conventions conv = temporal::fsr3_conventions(in.jitter_x, in.jitter_y, sign_x, sign_y);
    fr.jitter_x = conv.jitter_x;
    fr.jitter_y = conv.jitter_y;
    fr.mv_scale_x = conv.mv_scale_x;
    fr.mv_scale_y = conv.mv_scale_y;
    fr.reset = reset != temporal::Fsr3Reset::None;
    const temporal::Fsr3Sharpen sharpen = temporal::fsr3_sharpen(hs.upscale_sharpness);
    fr.sharpen = sharpen.enable;
    fr.sharpness = sharpen.sharpness;
    fr.frame_ms = in.frame_ms;
    const temporal::Fsr3Camera cam = temporal::fsr3_camera(in.near_plane, in.far_plane, in.fov_y);
    fr.near_plane = cam.near_plane;
    fr.far_plane = cam.far_plane;
    fr.fov_y = cam.fov_y;
    fr.frame_id = g.flushes + 1;  // this recording's serial + 1: complete once completed_submits reaches it
    // Every input was written by commands before this point.
    barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    const bool dump = dump_begin_locked(in, *target, out, "FSR 3.1", fr.reset);
    // test4: straight into the target when it took STORAGE (fsr3_target_storage_wanted).
    const bool direct = temporal::direct_output_ok(target->upscaler_storage, static_cast<int>(target->format), target->width, target->height, out,
                                                   g_fsr->output_w(), g_fsr->output_h(), dump);
    if (direct) fr.output = {target->image, target->format, target->width, target->height, g_fsr->output_w(), g_fsr->output_h()};
    const std::int32_t slot = pass_timer_begin_locked(cmd, TimedPass::Upscale);
    std::string why;
    const bool ok = g_fsr->record(fr, &why);
    pass_timer_end_locked(cmd, slot);
    if (!ok) {
        if (dump) fsr_dump_finish_locked(in.frame);
        static int said = 0;
        if (said++ < 8) host_log("fsr3: %s", why.c_str());
        g_history = {};  // the next dispatch starts over
        return fall_back(Fallback::Dispatch);
    }
    if (direct) {
        // FSR's writes before whatever reads the target next (the game's post-processing).
        barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
        ++g_window.direct;
        g_last_direct = target->image;
        static bool said = false;
        if (!said) {
            said = true;
            host_log("fsr3: FSR 3.1 writes the scale pass's %ux%u target directly (no copy of its output; HELIAS_FSR_DIRECT_TARGET=0: the copy)",
                     target->width, target->height);
        }
    } else {
        g_last_direct = VK_NULL_HANDLE;
        const std::int32_t copy_slot = pass_timer_begin_locked(cmd, TimedPass::Copy);
        copy_output(cmd, g_fsr->output(), g_fsr->output_format(), g_fsr->output_w(), g_fsr->output_h(), *target, out);
        pass_timer_end_locked(cmd, copy_slot);
    }
    pass_timer_upscaler_name("FSR 3.1");
    ++g_upscaled;
    dump_end_locked(dump, in.frame, g_fsr->output(), g_fsr->output_format(), g_fsr->output_w(), g_fsr->output_h(), *target);
    g_last_frame = in.frame;
    ++g_window.dispatches;
    if (g_last_fallback != Fallback::None) {
        if (g_last_fallback != Fallback::kCount) host_log("fsr3: upscaling again at flip %llu", static_cast<unsigned long long>(in.frame));
        else
        {
            // test2's "jitter 1 phases" was this first frame's: drawn before the
            // phases were set (the jitter starts on the next frame).
            char jitter[96];
            if (in.jitter_phases > 1)
                std::snprintf(jitter, sizeof(jitter), "jitter phase %u of %u", in.jitter_phase, in.jitter_phases);
            else
                std::snprintf(jitter, sizeof(jitter), "no jitter on this first frame (it starts with the next)");
            host_log("fsr3: first FSR 3.1 frame at flip %llu: scene %ux%u -> %ux%u at %u,%u in a %ux%u target, %s, reactive mask %s, sharpening %s %.1f",
                     static_cast<unsigned long long>(in.frame), in.render_w, in.render_h, out.w, out.h, out.x, out.y, target->width, target->height,
                     jitter, fr.reactive.image ? "on" : hs.upscale_reactive ? "on (none this frame)" : "off", fr.sharpen ? "on" : "off",
                     static_cast<double>(fr.sharpness));
            host_log("fsr3:   inputs (image, the region read): colour %ux%u (%ux%u), depth copy %ux%u (%ux%u), motion %ux%u (%ux%u), reactive %ux%u",
                     in.color.width, in.color.height, in.color.used_w, in.color.used_h, in.depth_copy.width, in.depth_copy.height,
                     in.depth_copy.used_w, in.depth_copy.used_h, in.motion.width, in.motion.height, in.motion.used_w, in.motion.used_h,
                     fr.reactive.width, fr.reactive.height);
        }
        g_last_fallback = Fallback::None;
    }
    std::uint64_t frames = g_window.dispatches;
    for (std::uint64_t v : g_window.fallbacks) frames += v;
    if (frames >= 3000) {
        host_log("%s", fsr3_report_locked().c_str());
        g_window = Stats{};
    }
    return true;
}

bool fsr4_replay_unavailable() { return g_fsr4_off; }

bool fsr3_target_storage_wanted(int format, std::uint32_t w, std::uint32_t h, std::uint32_t layers) {
    static const bool off = [] {
        const char* e = std::getenv("HELIAS_FSR_DIRECT_TARGET");
        return e && e[0] == '0';
    }();
    unsigned dw = 0, dh = 0;
    live_resolution_display(&dw, &dh);
    const int kind = host_settings().upscaler;
    const bool want = temporal::direct_target_wanted(format, w, h, layers, dw, dh, scene_split_available(),
                                                     upscaler::is_temporal(kind), off);
    if (want) {
        static bool said = false;
        if (!said) {
            said = true;
            host_log("fsr3: colour targets of %ux%u (RGBA16F) take STORAGE so FSR 3.1 can write the scale pass's directly", w, h);
        }
    }
    return want;
}

std::string fsr3_report_locked() {
    const Stats& w = g_window;
    std::string falls;
    for (int k = 1; k < static_cast<int>(Fallback::kCount); ++k) {
        if (!w.fallbacks[k]) continue;
        char part[160];
        std::snprintf(part, sizeof(part), "%s%llu %s", falls.empty() ? "" : ", ", static_cast<unsigned long long>(w.fallbacks[k]),
                      fallback_name(static_cast<Fallback>(k)));
        falls += part;
    }
    char buf[640];
    std::snprintf(buf, sizeof(buf), "fsr3: %llu dispatches (%llu FSR 4.1.1, %llu written directly, %llu copied, %llu blitted, %llu repeats), %llu resets, %llu contexts made, %.1f MiB; "
                  "bilinear frames: %s; GPU per pass: %s",
                  static_cast<unsigned long long>(w.dispatches), static_cast<unsigned long long>(w.fsr4), static_cast<unsigned long long>(w.direct),
                  static_cast<unsigned long long>(w.copies), static_cast<unsigned long long>(w.blits),
                  static_cast<unsigned long long>(w.repeats), static_cast<unsigned long long>(w.resets), static_cast<unsigned long long>(w.contexts),
                  g_fsr ? static_cast<double>(g_fsr->memory_bytes()) / (1024.0 * 1024.0) : 0.0, falls.empty() ? "none" : falls.c_str(),
                  [] { const std::string t = host_gpu_pass_timing(); return t.empty() ? std::string("not timed") : t; }().c_str());
    return buf;
}

}  // namespace gpu

std::string host_gpu_upscale_timing() { return host_gpu_pass_timing(); }

#else  // !HELIAS_HAVE_FSR_VULKAN: upscaler_scene_pass() never asks for FSR 3.1

namespace gpu {
bool fsr3_scale_pass_locked(RtImage*, const temporal::OutputRect&, const UpscalerImage*) { return false; }
bool fsr3_target_storage_wanted(int, std::uint32_t, std::uint32_t, std::uint32_t) { return false; }
bool fsr4_replay_unavailable() { return true; }
temporal::OutputRect fsr3_output_rect_locked(const RtImage*, const float*) { return {}; }
std::string fsr3_report_locked() { return "fsr3: not built (HELIAS_FSR_VULKAN=OFF)"; }
}  // namespace gpu
std::string host_gpu_upscale_timing() { return std::string(); }

#endif
