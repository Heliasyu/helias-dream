#pragma once

// heliasdream-vk Phase 4: FSR 4.1.1 at the scale pass, replaying AMD's 4.1.1
// upscaler DLL's passes on Vulkan (host/fsr411/fsr411.h, adapted from
// bbport). FSR 4.1.1 cannot be shipped: its SPIR-V and weights are built once
// from the user's own amd_fidelityfx_upscaler_dx12.dll (tools/fsr4cap) into
// the asset folder (HELIAS_FSR411_DIR, default
// ~/.local/share/HeliasDream/fsr4_411). Without them, or on a GPU without the
// features its passes need, FSR 4.1.1 is FSR 3.1 (host/fsr3_pass.h), and the
// log says why once.

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "host/upscaler_inputs.h"

namespace gpu {


// Before vkCreateDevice: only when the FSR 4.1.1 assets are there (or
// HELIAS_FSR4_FEATURES=1), the extensions and features its passes need, where
// the GPU has them. Nothing otherwise: a device without assets is stock.
void fsr4_device_features(VkPhysicalDeviceFeatures& f, VkPhysicalDeviceVulkan12Features& f12,
                          VkPhysicalDeviceVulkan13Features& f13, std::vector<const char*>& exts, void** chain);

// The asset folder FSR 4.1.1 reads, and whether a variant's sets are in it.
std::string fsr4_assets_dir();
bool fsr4_assets_present(const std::string& dir);

struct Fsr4Params {
    float jitter_x = 0.0f, jitter_y = 0.0f;
    bool reset = false, sharpen = false;
    float sharpness = 0.0f;
    bool ultra_performance = false;  // the quality preset is Ultra Performance (the second model)
    // test5: write here instead (the scale pass's target with STORAGE, RGBA16F, General,
    // exactly the output's size: fsr3_pass's direct_output_ok); *out is then this image.
    VkImage direct_image = VK_NULL_HANDLE;
    VkImageView direct_view = VK_NULL_HANDLE;
};

// Under g.mu, recording, outside a render pass, the inputs' writes ordered
// before: FSR 4.1.1 upscales into its own RGBA16F image (`*out`, in layout
// General) for the caller to copy. False: it cannot this frame; `fatal` set
// when it will not in this session (assets, device): the caller uses FSR 3.1.
bool fsr4_record_locked(VkCommandBuffer cmd, const UpscalerInputs& in, std::uint32_t output_w, std::uint32_t output_h,
                        const Fsr4Params& p, VkImage* out, bool* fatal, std::string* why);
// "FP8 t2160_m0 output 3840x2160", or empty before the first frame.
std::string fsr4_describe_locked();

}  // namespace gpu
