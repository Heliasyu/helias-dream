// heliasdream-vk Phase 0: the upscaler presets' arithmetic (host/upscaler_presets.h)
// and, when FSR-Vulkan is linked (cmake/fsr_vulkan.cmake), that its FSR 3.1.5
// shader tables are in the binary - all eleven passes' SPIR-V.
#include "host/upscaler_presets.h"

#include <cmath>
#include <cstdio>
#include <string_view>

#if defined(UPSCALER_TEST_FSR3)
#include "host/upscaler.h"
#endif

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                               \
        }                                                           \
    } while (0)

int main() {
    using namespace upscaler;
    std::uint32_t w = 0, h = 0;
    render_size(kPerformance, 3840, 2160, &w, &h);
    CHECK(w == 1920 && h == 1080);
    render_size(kQuality, 3840, 2160, &w, &h);
    CHECK(w == 2560 && h == 1440);
    render_size(kNativeAa, 2560, 1440, &w, &h);
    CHECK(w == 2560 && h == 1440);
    render_size(kUltraPerformance, 3840, 2160, &w, &h);
    CHECK(w == 1280 && h == 720);
    render_size(kBalanced, 3840, 2160, &w, &h);
    CHECK(w == 2259 && h == 1271);
    render_size(kUltraPerformance, 1, 1, &w, &h);
    CHECK(w == 1 && h == 1);
    // Sharpness 0.8 is stock FSR 1's 0.2 RCAS stops.
    CHECK(std::fabs(rcas_stops(0.8f) - 0.2f) < 1e-6f);
    CHECK(rcas_stops(2.0f) == 0.0f && rcas_stops(-1.0f) == 1.0f);
    CHECK(std::string_view(kind_name(kFsr1)) == "FSR 1");
    CHECK(std::string_view(preset_name(kUltraPerformance)) == "Ultra Performance");
    CHECK(std::string_view(kind_name(9)) == "?");
#if defined(UPSCALER_TEST_FSR3)
    CHECK(upscaler_fsr3_linked());
    const int modules = upscaler_fsr3_embedded_modules();
    std::printf("FSR 3.1.5 embedded SPIR-V modules: %d of 11\n", modules);
    CHECK(modules == 11);
#endif
    if (g_fail) return 1;
    std::printf("upscaler_test: ok\n");
    return 0;
}
