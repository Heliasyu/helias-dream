#pragma once

// Helia's Dream: FSR 4 at the scale pass - AMD's FSR 4 INT8 model ("v07",
// the source AMD published under MIT) through FireBurn/FSR-Vulkan's portable
// provider (third_party/FSR-Vulkan, ffx_vk_fsr4_v07*). The provider's SPIR-V
// and weights are not part of this repository: the launcher downloads the
// asset set Q2RTX builds from that source into the asset folder
// (HELIAS_FSR4V07_DIR, default ~/.local/share/HeliasDream/fsr4_v07). Without them,
// or on a GPU without INT8 dot products, FSR 4 is FSR 3.1 (host/fsr3_pass.h)
// and the log says why once. It runs on any GPU with INT8 dot products
// (RDNA 2 and later, Turing and later, Arc); FSR 4.1.1 (host/fsr4_pass.h) is
// AMD's newer model.

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "host/fsr4_pass.h"
#include "host/upscaler_inputs.h"

namespace gpu {

// Before vkCreateDevice: only when the FSR 4 assets are there, the features
// its passes need (INT8 dot products, 8- and 16-bit storage, FP16, compute
// derivatives for the pre-pass and auto-exposure), where the GPU has them.
// Nothing otherwise. After fsr4_device_features: its derivatives are reused.
void fsr4v07_device_features(VkPhysicalDeviceFeatures& f, VkPhysicalDeviceVulkan12Features& f12,
                             VkPhysicalDeviceVulkan13Features& f13, std::vector<const char*>& exts, void** chain);

std::string fsr4v07_assets_dir();
bool fsr4v07_assets_present(const std::string& dir);

// As fsr4_record_locked (host/fsr4_pass.h), with the same parameters.
bool fsr4v07_record_locked(VkCommandBuffer cmd, const UpscalerInputs& in, std::uint32_t output_w, std::uint32_t output_h,
                           const Fsr4Params& p, VkImage* out, bool* fatal, std::string* why);
// "balanced t2160 output 3840x2160", or empty before the first frame.
std::string fsr4v07_describe_locked();

}  // namespace gpu
