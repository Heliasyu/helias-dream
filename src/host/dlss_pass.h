#pragma once

// Helia's Dream: NVIDIA DLSS Super Resolution at the scale pass, through the
// NGX core the NVIDIA driver installs (libnvidia-ngx.so.1 on Linux,
// _nvngx.dll on Windows), loaded at run time - no NVIDIA SDK at build time.
// The DLSS model is NVIDIA's redistributable nvngx_dlss (libnvidia-ngx-dlss.so
// / nvngx_dlss.dll), searched in HELIAS_DLSS_DIR and next to the executable.
// Adapted from Lantern Engine's vk_dlss.cpp (Awakenchan, GPL-2.0-or-later).
//
// On a GPU or driver without DLSS (AMD, Intel, no NGX core) it says why once
// and FSR 3.1 stands in (host/fsr3_pass.h). On Windows, OptiScaler can stand
// in for NVIDIA's NGX core and run this path on any GPU.

#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "host/fsr4_pass.h"
#include "host/upscaler_inputs.h"

namespace gpu {

// Before vkCreateDevice, with DLSS the chosen upscaler: NGX's device
// extensions (VK_NVX_binary_import, VK_NVX_image_view_handle) where the GPU
// has them. Nothing otherwise.
void dlss_device_extensions(std::vector<const char*>& exts);
// Whether the device was made with NGX's extensions: DLSS chosen at the start.
bool dlss_device_ready();

// As fsr4_record_locked (host/fsr4_pass.h). `preset`: the upscale quality
// (0 Native AA - DLAA - .. 4 Ultra Performance).
bool dlss_record_locked(VkCommandBuffer cmd, const UpscalerInputs& in, std::uint32_t output_w, std::uint32_t output_h, const Fsr4Params& p,
                        int preset, VkImage* out, bool* fatal, std::string* why);
std::string dlss_describe_locked();

}  // namespace gpu
