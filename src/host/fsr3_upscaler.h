#pragma once

// heliasdream-vk Phase 3: FSR 3.1.5 (FireBurn/FSR-Vulkan, MIT) as one object that
// owns a context and the images FSR keeps between frames, and records a
// frame's upscale into a command buffer. It knows nothing of the renderer:
// host/fsr3_pass.cpp drives it from the game's scale pass, and
// tests/fsr3_upscaler_test.cpp drives it on a software Vulkan device.
//
// Images it is given are imported per frame (FSR-Vulkan views them) and
// released once that frame's submission is known complete (retire()); the
// images it owns - FSR's three shared images and the output - are imported
// once. Every image it touches is in VK_IMAGE_LAYOUT_GENERAL before and
// after record(); the bridge transitions inside and restores.

#include "host/upscaler_inputs.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

struct Fsr3Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    UpscalerImage color;     // HDR scene colour, render size used from (0, 0)
    UpscalerImage depth;     // R32F, D3D depth 0..1 (not reversed)
    UpscalerImage motion;    // render pixels, current to previous (rg)
    UpscalerImage reactive;  // R8 0..1, optional
    UpscalerImage output;    // test4: write here (RGBA16F, STORAGE) instead of the owned output; optional
    float jitter_x = 0.0f, jitter_y = 0.0f;  // FSR's jitterOffset (render pixels)
    float mv_scale_x = 1.0f, mv_scale_y = 1.0f;
    bool reset = false;
    bool sharpen = false;
    float sharpness = 0.0f;
    float frame_ms = 16.7f;
    float near_plane = 0.05f, far_plane = 3000.0f, fov_y = 1.0f;
    // Nonzero and never decreasing: the submission this is recorded into.
    // retire(n) means every frame_id <= n has finished on the GPU.
    std::uint64_t frame_id = 0;
};

struct FfxVkFsr3_3_1_5UpscalerContext;

class Fsr3Upscaler {
public:
    Fsr3Upscaler() = default;
    ~Fsr3Upscaler();
    Fsr3Upscaler(const Fsr3Upscaler&) = delete;
    Fsr3Upscaler& operator=(const Fsr3Upscaler&) = delete;

    // A context for exactly these sizes, its shared images and an RGBA16F
    // output at the output size. False with `why` set: nothing is left made.
    bool create(VkPhysicalDevice phys, VkDevice device, std::uint32_t render_w, std::uint32_t render_h, std::uint32_t output_w,
                std::uint32_t output_h, std::string* why);
    bool ready() const { return context_ != nullptr; }

    // Records the upscale of one frame. The caller orders the inputs' writes
    // before it and the output's reads after it (barriers).
    bool record(const Fsr3Frame& f, std::string* why);
    // Releases what frames through `completed_id` held.
    void retire(std::uint64_t completed_id);
    // The GPU must be done with everything recorded (retire past the last
    // frame_id, or the device idle).
    void destroy();

    VkImage output() const { return output_.image; }
    VkFormat output_format() const { return VK_FORMAT_R16G16B16A16_SFLOAT; }
    std::uint32_t render_w() const { return render_w_; }
    std::uint32_t render_h() const { return render_h_; }
    std::uint32_t output_w() const { return output_w_; }
    std::uint32_t output_h() const { return output_h_; }
    std::uint64_t last_frame_id() const { return last_frame_; }
    // FSR's own allocation plus the images this object owns, in bytes.
    std::uint64_t memory_bytes() const { return memory_bytes_; }
    // The shared images' sizes and formats, for the log and the tests.
    struct SharedInfo {
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::uint32_t width = 0, height = 0;
    };
    SharedInfo shared_info(int k) const { return k >= 0 && k < 3 ? SharedInfo{shared_[k].format, shared_[k].w, shared_[k].h} : SharedInfo{}; }

private:
    struct Owned {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::uint32_t w = 0, h = 0;
        void* token = nullptr;  // its import
    };
    struct Imported {
        void* token = nullptr;
        std::uint64_t frame_id = 0;
    };
    bool make_owned(Owned& o, VkFormat format, std::uint32_t w, std::uint32_t h, std::string* why);
    void* import(const UpscalerImage& img, bool writable);

    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    FfxVkFsr3_3_1_5UpscalerContext* context_ = nullptr;
    Owned shared_[3];
    Owned output_;
    bool owned_general_ = false;
    std::vector<Imported> imports_;
    std::uint32_t render_w_ = 0, render_h_ = 0, output_w_ = 0, output_h_ = 0;
    std::uint64_t last_frame_ = 0, memory_bytes_ = 0;
};
