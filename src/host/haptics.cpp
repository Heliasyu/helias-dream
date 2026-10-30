// heliasdream-vk: DualSense / DualSense Edge HD haptics and adaptive triggers
// (host/haptics.h).
#include "host/haptics.h"
#include "core/host_clock.h"
#include "engine/world_chr.h"
#include "host/options.h"
#include "host/settings.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(HELIAS_HAVE_SDL3)
#include <SDL3/SDL.h>
#endif
#if defined(__linux__)
#include <fcntl.h>
#include <sound/asound.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

constexpr float kRate = 48000.0f;
constexpr float kTwoPi = 6.28318530718f;

// A one-pole follower's coefficient for a time constant in milliseconds.
float follow(float ms) { return 1.0f - std::exp(-1.0f / (ms * 0.001f * kRate)); }

// A second-order low-pass (the RBJ cookbook's), run twice for a steeper edge.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass(float fc, float q) {
        const float w = kTwoPi * fc / kRate, c = std::cos(w), alpha = std::sin(w) / (2.0f * q), a0 = 1.0f + alpha;
        b0 = (1.0f - c) / 2.0f / a0;
        b1 = (1.0f - c) / a0;
        b2 = b0;
        a1 = -2.0f * c / a0;
        a2 = (1.0f - alpha) / a0;
    }
    float run(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct Envelope {
    float attack = 0, release = 0, v = 0;
    float run(float x) {
        v += (x - v) * (x > v ? attack : release);
        return v;
    }
};

// What can be tuned between test rounds without a build:
// HELIAS_HAPTICS_TUNE="cutoff=160,sustain=0,floor=0.006,ratio=2,front=4,slow=800,expand=0.04,carrier=0.5,gain=3,rumble=0.8"
//   cutoff   the effects' low-pass, Hz (what the motors are given of the sound)
//   sustain  how much of a steady sound stays (0: only hits; 1: everything)
//   floor    the level below which a sound is not felt at all
//   ratio    how much louder than the last `slow` ms a sound must be to be a hit
//   front    the same for the front left and right, where the music is
//   slow     that window, ms - longer than a gust of wind
//   expand   what the effects give the motors below this fades to nothing (0: off)
//   carrier  0: the filtered sound itself; 1: a 150 Hz tone that follows it
//   gain     the effects' level before the soft limit
//   rumble   the converted rumble's level
// HELIAS_HAPTICS_CHANNELS="2,3": the device channels of the left and right motors.
struct Tuning {
    // Round 7: with 15% of every steady sound kept, a 1.4x rise over 400 ms
    // counted as a hit and a floor of 0.003, wind and ambience were felt as a
    // constant buzz. Only hits now: twice the last 800 ms, above a higher floor.
    float cutoff = 160.0f, sustain = 0.0f, floor = 0.006f, ratio = 2.0f, front = 4.0f, slow = 800.0f, expand = 0.04f, carrier = 0.5f,
          gain = 3.0f, rumble = 0.8f;
    int left = 2, right = 3;
};
Tuning read_tuning() {
    Tuning t;
    if (const char* e = std::getenv("HELIAS_HAPTICS_TUNE")) {
        std::string s = e;
        std::size_t at = 0;
        while (at < s.size()) {
            const std::size_t end = std::min(s.find(',', at), s.size());
            const std::string kv = s.substr(at, end - at);
            const std::size_t eq = kv.find('=');
            if (eq != std::string::npos) {
                const std::string k = kv.substr(0, eq);
                const float v = std::strtof(kv.c_str() + eq + 1, nullptr);
                if (k == "cutoff") t.cutoff = std::clamp(v, 40.0f, 1000.0f);
                else if (k == "sustain") t.sustain = std::clamp(v, 0.0f, 1.0f);
                else if (k == "floor") t.floor = std::clamp(v, 0.0f, 1.0f);
                else if (k == "carrier") t.carrier = std::clamp(v, 0.0f, 1.0f);
                else if (k == "ratio") t.ratio = std::clamp(v, 1.0f, 10.0f);
                else if (k == "front") t.front = std::clamp(v, 1.0f, 20.0f);
                else if (k == "expand") t.expand = std::clamp(v, 0.0f, 0.5f);
                else if (k == "slow") t.slow = std::clamp(v, 50.0f, 5000.0f);
                else if (k == "gain") t.gain = std::clamp(v, 0.0f, 50.0f);
                else if (k == "rumble") t.rumble = std::clamp(v, 0.0f, 2.0f);
            }
            at = end + 1;
        }
    }
    if (const char* e = std::getenv("HELIAS_HAPTICS_CHANNELS")) {
        int l = 0, r = 0;
        if (std::sscanf(e, "%d,%d", &l, &r) == 2 && l >= 0 && l < 4 && r >= 0 && r < 4) {
            t.left = l;
            t.right = r;
        }
    }
    return t;
}

// One motor's share of the effects: the side's low end, how loud it is now
// (fast) and over the last half second (slow), and how much of it is a hit.
struct Side {
    Biquad lp1, lp2;
    Envelope fast, slow, gate;
    float ratio = 2.0f;
    void init(const Tuning& t) {
        lp1.lowpass(t.cutoff, 0.707f);
        lp2.lowpass(t.cutoff, 0.707f);
        fast.attack = follow(1.0f);
        fast.release = follow(40.0f);
        slow.attack = follow(t.slow);
        slow.release = follow(t.slow);
        gate.attack = follow(2.0f);
        gate.release = follow(60.0f);
        ratio = t.ratio;
    }
    // The filtered sound and the envelope the tone follows, both shaped by
    // the transient gate.
    void run(float x, const Tuning& t, float* sound, float* level) {
        const float low = lp2.run(lp1.run(x));
        const float e = fast.run(std::fabs(low));
        const float s = slow.run(std::fabs(low));
        // A hit: `ratio` times louder now than the last `slow` ms have been.
        const float hit = std::clamp((e - ratio * s) / (e + 1e-6f), 0.0f, 1.0f);
        const float g = gate.run(hit);
        // Under the floor nothing is felt, with a soft knee to it.
        const float audible = std::clamp((e - t.floor) / (t.floor + 1e-6f), 0.0f, 1.0f);
        const float shape = (g + t.sustain) * audible;
        *sound = low * shape;
        *level = e * shape;
    }
};

#if defined(HELIAS_HAVE_SDL3)
std::mutex g_mu;  // everything below, from the game's audio thread and the shutdown
SDL_AudioStream* g_stream = nullptr;
SDL_AudioDeviceID g_device = 0;
bool g_audio_up = false;  // our SDL_InitSubSystem(SDL_INIT_AUDIO), to be paired with a quit
bool g_shut = false;
std::chrono::steady_clock::time_point g_last_scan{};
bool g_said_none = false;
std::vector<float> g_out;
#endif
std::atomic<bool> g_rescan{true};
std::atomic<bool> g_usb_pad{false};  // a DualSense connected by USB (haptics_devices_changed)

Tuning g_tune;
bool g_tune_read = false;
// The effects' two paths a motor each: the centre, the LFE, the surrounds and
// the backs at the usual gate, and the front pair - the music's own, with
// the ambience's stereo bed - at a far stricter one (round 8's recording: the
// title music and the Dream's ambience were all in FL/FR, and every in-game
// event showed in C, the LFE or the backs).
Side g_side[2], g_front[2];
// The expander after them (Tuning::expand): round 9's area music still came
// through faintly - its bass notes in every channel, each passing the gate -
// at a tenth of a hit's level. What stays under the threshold fades out.
Envelope g_expand[2];
float g_carrier_phase = 0.0f, g_large_phase = 0.0f, g_small_phase = 0.0f;
float g_large = 0.0f, g_small = 0.0f;  // the rumble's smoothed amplitudes

// The rumble the game asked for, and until when (its pad thread; a call holds
// for 100 ms, as the SDL rumble did).
std::atomic<std::uint32_t> g_rumble{0};  // small << 8 | large
std::atomic<std::int64_t> g_rumble_until_us{0};
std::atomic<bool> g_hd_live{false};  // an HD output is open and fed
std::atomic<std::uint32_t> g_hd_generation{0};  // HD outputs opened (haptics_hd_generation)

// Menu taps (haptics_menu_tick): the next one asked for, and the one playing.
std::atomic<int> g_tick_asked{-1};
std::atomic<std::int64_t> g_tick_last_us{0};  // moves closer than 35 ms are one tap
float g_tick_amp = 0.0f, g_tick_phase = 0.0f, g_tick_freq = 0.0f;
int g_tick_left = 0;  // samples still to play

std::int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The settings, re-read when they change.
struct Wanted {
    std::uint64_t serial = 0;
    int mode = 2;
    float strength = 1.0f;
    bool triggers = true;
    int trigger_strength = 1;
    bool menu = true;
};
std::mutex g_wanted_mu;
Wanted g_wanted;
Wanted wanted() {
    const std::uint64_t s = host_opt_serial();
    std::lock_guard<std::mutex> lock(g_wanted_mu);
    if (s != g_wanted.serial) {
        const HostSettings h = host_settings();
        g_wanted.serial = s;
        g_wanted.mode = h.haptics;
        g_wanted.strength = h.haptics_strength;
        g_wanted.triggers = h.adaptive_triggers;
        g_wanted.trigger_strength = h.trigger_strength;
        g_wanted.menu = h.menu_haptics;
        g_rescan = true;
    }
    return g_wanted;
}

// The controller's own playback volume, all the way up. Linux leaves the
// DualSense's ALSA "PCM Playback Volume" at 80 of 100 - on its -100..0 dB
// scale, -20 dB, a tenth of the motors' strength - and PipeWire does not
// raise it. Haptics strength (F10) is the level from there. A controller
// plugged in again gets its old level back from WirePlumber a moment after
// its card appears - after this ran - so it is checked again every few
// seconds while HD output is open (haptics_game_audio); `quiet`: say nothing
// when it was already at its maximum.
void max_controller_volume(bool quiet = false) {
#if defined(__linux__)
    std::FILE* f = std::fopen("/proc/asound/cards", "r");
    if (!f) return;
    char line[256];
    int card = -1;
    while (card < 0 && std::fgets(line, sizeof(line), f)) {
        int n = -1;
        if (std::strstr(line, "DualSense") && std::sscanf(line, " %d [", &n) == 1) card = n;
    }
    std::fclose(f);
    if (card < 0) return;
    const std::string path = "/dev/snd/controlC" + std::to_string(card);
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        if (!quiet) host_log("haptics: cannot open %s to raise the controller's volume", path.c_str());
        return;
    }
    bool changed = false;
    const auto set_max = [&](const char* name, bool boolean) {
        snd_ctl_elem_info info{};
        info.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
        std::snprintf(reinterpret_cast<char*>(info.id.name), sizeof(info.id.name), "%s", name);
        if (::ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0) return -1L;
        snd_ctl_elem_value v{};
        v.id = info.id;
        const long top = boolean ? 1 : info.value.integer.max;
        if (::ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &v) == 0) {
            bool at_top = true;
            for (unsigned i = 0; i < info.count && i < 128; ++i) at_top = at_top && v.value.integer.value[i] == top;
            if (at_top) return top;
        }
        for (unsigned i = 0; i < info.count && i < 128; ++i) v.value.integer.value[i] = top;
        if (::ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v) < 0) return -1L;
        changed = true;
        return top;
    };
    set_max("PCM Playback Switch", true);
    const long top = set_max("PCM Playback Volume", false);
    ::close(fd);
    if (!quiet || changed || top < 0)
        host_log("haptics: the controller's playback volume %s (card %d)",
                 top < 0 ? "could not be set" : changed ? "raised to its maximum" : "at its maximum", card);
#endif
}

#if defined(HELIAS_HAVE_SDL3)
bool contains_nocase(const char* hay, const char* needle) {
    if (!hay) return false;
    const std::size_t n = std::strlen(needle);
    for (; *hay; ++hay) {
        std::size_t i = 0;
        while (i < n && hay[i] && std::tolower(static_cast<unsigned char>(hay[i])) == std::tolower(static_cast<unsigned char>(needle[i]))) ++i;
        if (i == n) return true;
    }
    return false;
}

// The motors' channels of the controller's PipeWire sink at 100%. The sink
// keeps a volume per channel, restored at each plug-in, and the desktop's
// volume slider moves all four: lowered for the speaker, it weakens the
// haptics too. The speaker's two are left as they are; a muted sink is
// unmuted with the speaker's two at 0, so it stays silent. Through pactl
// (PipeWire's or PulseAudio's), on a thread of its own: a missing pactl or
// sink only costs a log line.
void motor_channels_full() {
#if defined(__linux__)
    std::thread([] {
        host_thread_place(HostPlace::Cold, "bb-haptics-vol");
        const auto run = [](const std::string& cmd) {
            std::string out;
            if (std::FILE* p = ::popen(cmd.c_str(), "r")) {
                char buf[512];
                while (std::fgets(buf, sizeof(buf), p)) out += buf;
                ::pclose(p);
            }
            return out;
        };
        std::string sink;
        const std::string sinks = run("pactl list short sinks 2>/dev/null");
        for (std::size_t at = 0; at < sinks.size() && sink.empty();) {
            const std::size_t end = sinks.find('\n', at);
            const std::string line = sinks.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? sinks.size() : end + 1;
            if (!contains_nocase(line.c_str(), "dualsense") || line.find("4ch") == std::string::npos) continue;
            const std::size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
            if (t2 != std::string::npos) sink = line.substr(t1 + 1, t2 - t1 - 1);
        }
        // A name as PipeWire makes them; anything else is not put on a command line.
        if (sink.empty() || sink.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._:-") != std::string::npos) {
            host_log("haptics: no DualSense sink through pactl - its channel volumes are left as they are");
            return;
        }
        // "Volume: front-left: 26214 /  40% / -23.88 dB,   front-right: ...": the channels in the sink's order.
        const std::string vol = run("pactl get-sink-volume " + sink + " 2>/dev/null");
        std::vector<std::pair<std::string, long>> ch;
        for (std::size_t at = vol.find("Volume:"); at != std::string::npos;) {
            const std::size_t colon = vol.find(": ", at + 7);
            if (colon == std::string::npos) break;
            std::size_t name_at = vol.find_last_of(" ,", colon - 1);
            name_at = name_at == std::string::npos ? 0 : name_at + 1;
            const std::string name = vol.substr(name_at, colon - name_at);
            const long v = std::strtol(vol.c_str() + colon + 2, nullptr, 10);
            if (name.find('-') != std::string::npos) ch.emplace_back(name, v);
            at = vol.find(',', colon);
            if (at != std::string::npos && vol.find(':', at) == std::string::npos) break;
        }
        const bool muted = run("pactl get-sink-mute " + sink + " 2>/dev/null").find("yes") != std::string::npos;
        bool low = muted;
        for (const auto& c : ch) low = low || (c.first.rfind("rear-", 0) == 0 && c.second < 65536);
        if (ch.size() != 4 || !low) {
            if (ch.size() != 4) host_log("haptics: the DualSense sink has %zu channels through pactl, not 4 - left as it is", ch.size());
            return;
        }
        std::string cmd = "pactl set-sink-volume " + sink;
        for (const auto& c : ch) cmd += " " + std::to_string(c.first.rfind("rear-", 0) == 0 ? 65536L : muted ? 0L : c.second);
        run(cmd + " 2>/dev/null");
        if (muted) run("pactl set-sink-mute " + sink + " 0 2>/dev/null");
        std::string was;
        for (const auto& c : ch)
            if (c.first.rfind("rear-", 0) == 0) was += (was.empty() ? "" : " and ") + std::to_string(c.second * 100 / 65536) + "%";
        host_log("haptics: the controller's motor channels raised to 100%%%s (they were %s)", muted ? ", the sink unmuted with its speaker at 0" : "",
                 was.c_str());
    }).detach();
#endif
}

void close_locked(const char* why) {
    if (!g_stream) return;
    SDL_DestroyAudioStream(g_stream);
    g_stream = nullptr;
    g_device = 0;
    g_hd_live = false;
    host_log("haptics: HD output closed (%s)", why);
}

// The controller's sound card: a playback device named DualSense (or Wireless
// Controller) with at least four channels. Only while plugged in by USB is there one.
void scan_locked(bool want_hd) {
    g_last_scan = std::chrono::steady_clock::now();
    if (!want_hd) {
        close_locked("Haptics is not HD, or no DualSense is on USB");
        return;
    }
    if (!g_audio_up) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            host_log("haptics: no SDL audio (%s); the classic rumble runs", SDL_GetError());
            return;
        }
        g_audio_up = true;
    }
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    SDL_AudioDeviceID found = 0;
    const char* found_name = nullptr;
    for (int i = 0; ids && i < count && !found; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        // Windows often names it "Speakers (Wireless Controller)"; the four
        // channels below keep a DualShock 4's two-channel one out.
        if (!contains_nocase(name, "dualsense") && !contains_nocase(name, "wireless controller")) continue;
        SDL_AudioSpec spec{};
        int frames = 0;
        if (SDL_GetAudioDeviceFormat(ids[i], &spec, &frames) && spec.channels >= 4) {
            found = ids[i];
            found_name = name;
        }
    }
    if (g_stream && found != g_device) close_locked("the controller's sound card is gone");
    if (found && !g_stream) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 4;
        spec.freq = static_cast<int>(kRate);
        // A short device buffer (SDL's default is 1024 frames, 21 ms): a hit
        // felt after it is heard feels wrong. Only for this device - the hint
        // is read as a device opens, and the game's own output opened long
        // before.
        const char* had = SDL_GetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
        const std::string kept = had ? had : "";
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
        g_stream = SDL_OpenAudioDeviceStream(found, &spec, nullptr, nullptr);
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, had ? kept.c_str() : nullptr);
        if (!g_stream) {
            host_log("haptics: %s would not open (%s); the classic rumble runs", found_name ? found_name : "the DualSense", SDL_GetError());
            return;
        }
        SDL_ResumeAudioStreamDevice(g_stream);
        g_device = found;
        g_hd_live = true;
        g_hd_generation.fetch_add(1, std::memory_order_release);
        g_said_none = false;
        max_controller_volume();
        motor_channels_full();
        host_log("haptics: HD output on \"%s\" (motors on channels %d and %d of 4)", found_name ? found_name : "?", g_tune.left + 1,
                 g_tune.right + 1);
        // PipeWire's Pro Audio profile names the four channels AUX0..AUX3, and
        // a stream's four (FL FR RL RR) are then mixed down onto the first two -
        // the speaker - so the motors get nothing. Its Direct profile maps them.
        const std::size_t len = found_name ? std::strlen(found_name) : 0;
        if (len > 4 && std::strcmp(found_name + len - 4, " Pro") == 0) {
            host_log("haptics: the controller's sound card is in the Pro Audio profile, where the motors cannot be reached - "
                     "switch it to Direct (System Settings > Sound, or: pactl set-card-profile <its card> Direct)");
        }
    } else if (!found && !g_said_none) {
        g_said_none = true;
        host_log("haptics: no DualSense sound card (HD haptics need the controller plugged in by USB); the classic rumble runs");
    }
    SDL_free(ids);
}
#endif

// One pass over a buffer: the effects and the rumble, into the motors.
void render(const float* in, unsigned count, float gain, const Wanted& w, float* out) {
    const std::uint32_t r = g_rumble.load(std::memory_order_relaxed);
    const bool held = now_us() < g_rumble_until_us.load(std::memory_order_relaxed);
    const float want_large = held ? std::pow(static_cast<float>(r & 0xff) / 255.0f, 0.7f) : 0.0f;
    const float want_small = held ? std::pow(static_cast<float>((r >> 8) & 0xff) / 255.0f, 0.7f) : 0.0f;
    const float smooth = follow(4.0f);
    const float strength = w.strength;
    const Tuning& t = g_tune;
    // A menu tap: a few cycles of a high tone, gone in 15-25 ms. A move is
    // the lightest; a decide a little firmer and lower; a back in between.
    // Round 7: the moves were a tad light (0.30); the decide was liked.
    // Round 18: all ten points up (move 0.55, decide 0.65, back 0.60).
    if (const int k = g_tick_asked.exchange(-1); k >= 0 && w.menu) {
        static constexpr float kAmp[3] = {0.55f, 0.65f, 0.60f}, kFreq[3] = {230.0f, 170.0f, 200.0f}, kMs[3] = {15.0f, 25.0f, 20.0f};
        g_tick_amp = kAmp[k];
        g_tick_freq = kFreq[k];
        g_tick_phase = 0.0f;
        g_tick_left = static_cast<int>(kMs[k] * 0.001f * kRate);
    }
    const float tick_decay = std::exp(-1.0f / (0.006f * kRate));
    for (unsigned i = 0; i < count; ++i) {
        const float* f = in ? in + i * 8 : nullptr;
        float sides[2] = {0.0f, 0.0f}, levels[2] = {0.0f, 0.0f};
        if (f) {
            // The centre, the LFE, the surrounds and the backs: the game's
            // events. The fronts on their own path (g_front).
            const float rest_l = (0.6f * f[2] + f[3] + 0.7f * f[4] + f[6]) * gain;
            const float rest_r = (0.6f * f[2] + f[3] + 0.7f * f[5] + f[7]) * gain;
            float fs[2], fl[2];
            g_side[0].run(rest_l, t, &sides[0], &levels[0]);
            g_side[1].run(rest_r, t, &sides[1], &levels[1]);
            g_front[0].run(f[0] * gain, t, &fs[0], &fl[0]);
            g_front[1].run(f[1] * gain, t, &fs[1], &fl[1]);
            for (int s = 0; s < 2; ++s) {
                sides[s] += fs[s];
                levels[s] += fl[s];
            }
        }
        g_carrier_phase += kTwoPi * 150.0f / kRate;
        if (g_carrier_phase > kTwoPi) g_carrier_phase -= kTwoPi;
        const float tone = std::sin(g_carrier_phase);
        // The rumble: the large motor a 70 Hz tone, the small one 160 Hz.
        g_large += (want_large - g_large) * smooth;
        g_small += (want_small - g_small) * smooth;
        g_large_phase += kTwoPi * 70.0f / kRate;
        g_small_phase += kTwoPi * 160.0f / kRate;
        if (g_large_phase > kTwoPi) g_large_phase -= kTwoPi;
        if (g_small_phase > kTwoPi) g_small_phase -= kTwoPi;
        const float rumble = t.rumble * (0.65f * g_large * std::sin(g_large_phase) + 0.35f * g_small * std::sin(g_small_phase));
        float tick = 0.0f;
        if (g_tick_left > 0) {
            --g_tick_left;
            g_tick_phase += kTwoPi * g_tick_freq / kRate;
            tick = g_tick_amp * std::sin(g_tick_phase);
            g_tick_amp *= tick_decay;
        }
        float motor[2];
        for (int s = 0; s < 2; ++s) {
            float fx = ((1.0f - t.carrier) * sides[s] + t.carrier * 2.0f * levels[s] * tone) * t.gain;
            if (t.expand > 0.0f) fx *= std::clamp((g_expand[s].run(std::fabs(fx)) - t.expand) / t.expand, 0.0f, 1.0f);
            // The tap keeps its own level: the strength setting is for the game.
            motor[s] = std::tanh((fx + rumble) * strength + tick) * 0.95f;
        }
        float* o = out + i * 4;
        o[0] = o[1] = o[2] = o[3] = 0.0f;
        o[t.left] = motor[0];
        o[t.right] = motor[1];
    }
}

}  // namespace

int haptics_mode() { return wanted().mode; }

std::uint32_t haptics_hd_generation() {
    return g_hd_live.load(std::memory_order_acquire) ? g_hd_generation.load(std::memory_order_acquire) : 0;
}

bool haptics_is_dualsense(std::uint16_t vendor, std::uint16_t product) {
    return vendor == 0x054c && (product == 0x0ce6 || product == 0x0df2);
}

void haptics_devices_changed(bool dualsense_on_usb) {
    g_usb_pad = dualsense_on_usb;
    g_rescan = true;
}

float haptics_r2_fire_point() {
    static const float fire = [] {
        const char* e = std::getenv("HELIAS_R2_FIRE");
        const float v = e && *e ? std::strtof(e, nullptr) : 0.0f;
        return v > 0.05f && v < 1.0f ? v : 0.55f;  // just into the wall that starts at zone 4 of 10
    }();
    return wanted().triggers ? fire : 0.0f;
}

// Whether the gun is known to be empty (round 13's recording): its pull plays
// 117054500 with no Quicksilver Bullets, 117051000 when a shot goes. Set by an
// empty pull; cleared by a shot, by Blood Bullets (100141), and by any menu or
// loading screen - where bullets are taken from storage or restocked at a
// lamp. The pump thread only.
bool g_gun_empty = false;
// Empty: no Quicksilver Bullets carried (engine/world_chr.h, read from the
// inventory - round 16), or, when that cannot be read, the animations' guess.
bool gun_empty() {
    const int bullets = world_player_bullets();
    return bullets >= 0 ? bullets == 0 : g_gun_empty;
}

float haptics_l2_fire_point() {
    static const float fire = [] {
        const char* e = std::getenv("HELIAS_L2_FIRE");
        const float v = e && *e ? std::strtof(e, nullptr) : 0.0f;
        return v > 0.05f && v < 1.0f ? v : 0.65f;  // the break (zone 6 of 10) gives way just before
    }();
    // Empty, L2 is slack: no break to wait for, the usual light pull.
    return wanted().triggers && !gun_empty() ? fire : 0.0f;
}

void haptics_menu_tick(int kind) {
    if (kind < 0 || kind > 2 || !g_hd_live.load(std::memory_order_relaxed)) return;
    const std::int64_t now = now_us();
    if (kind == 0 && now - g_tick_last_us.load(std::memory_order_relaxed) < 35000) return;  // a held direction scrolling: not a buzz
    g_tick_last_us.store(now, std::memory_order_relaxed);
    g_tick_asked.store(kind, std::memory_order_relaxed);
}

bool haptics_rumble(std::uint8_t small, std::uint8_t large) {
    if (!g_hd_live.load(std::memory_order_relaxed) || wanted().mode != 2) return false;
    g_rumble.store(static_cast<std::uint32_t>(small) << 8 | large, std::memory_order_relaxed);
    g_rumble_until_us.store(now_us() + 100000, std::memory_order_relaxed);
    return true;
}

void haptics_game_audio(const float* frames, unsigned count, float gain) {
#if defined(HELIAS_HAVE_SDL3)
    const Wanted w = wanted();
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_shut) return;
    if (!g_tune_read) {
        g_tune_read = true;
        g_tune = read_tuning();
        for (Side& s : g_side) s.init(g_tune);
        for (Side& s : g_front) {
            s.init(g_tune);
            s.ratio = g_tune.front;
        }
        for (Envelope& e : g_expand) {
            e.attack = follow(1.0f);
            e.release = follow(50.0f);
        }
        if (std::getenv("HELIAS_HAPTICS_TUNE") || std::getenv("HELIAS_HAPTICS_CHANNELS")) {
            host_log("haptics: tuning cutoff %.0f Hz, sustain %.2f, floor %.4f, ratio %.2f (front %.2f), slow %.0f ms, expand %.3f, carrier %.2f, gain %.2f, "
                     "rumble %.2f; motors on channels %d,%d",
                     static_cast<double>(g_tune.cutoff), static_cast<double>(g_tune.sustain), static_cast<double>(g_tune.floor),
                     static_cast<double>(g_tune.ratio), static_cast<double>(g_tune.front), static_cast<double>(g_tune.slow),
                     static_cast<double>(g_tune.expand),
                     static_cast<double>(g_tune.carrier), static_cast<double>(g_tune.gain), static_cast<double>(g_tune.rumble),
                     g_tune.left + 1, g_tune.right + 1);
        }
    }
    const bool want_hd = w.mode == 2 && g_usb_pad.load(std::memory_order_relaxed);
    // Looked for when something changed, and every 3 s while HD is wanted
    // and there is no output yet (the controller may be plugged in later).
    if (g_rescan.exchange(false) || (want_hd && !g_stream && std::chrono::steady_clock::now() - g_last_scan > std::chrono::seconds(3))) {
        scan_locked(want_hd);
    }
    if (!g_stream || count == 0) return;
    static std::chrono::steady_clock::time_point volume_checked{};
    if (const auto now = std::chrono::steady_clock::now(); now - volume_checked > std::chrono::seconds(2)) {
        volume_checked = now;
        max_controller_volume(true);
    }
    g_out.resize(static_cast<std::size_t>(count) * 4);
    render(frames, count, gain, w, g_out.data());
    // Felt late is felt wrong: past 30 ms queued (the device stalled or the
    // game's audio ran ahead), the queue starts over.
    const int bytes = static_cast<int>(g_out.size() * sizeof(float));
    if (SDL_GetAudioStreamQueued(g_stream) > static_cast<int>(0.030f * kRate) * 4 * static_cast<int>(sizeof(float))) {
        SDL_ClearAudioStream(g_stream);
    }
    SDL_PutAudioStreamData(g_stream, g_out.data(), bytes);
#else
    (void)frames;
    (void)count;
    (void)gain;
#endif
}

void haptics_shutdown() {
#if defined(HELIAS_HAVE_SDL3)
    std::lock_guard<std::mutex> lock(g_mu);
    g_shut = true;
    close_locked("shutting down");
    if (g_audio_up) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        g_audio_up = false;
    }
#endif
}

// The trigger effect blocks. Ten zones along the travel, 0 at rest and 9 at
// the end; each mode's bytes as the firmware reads them (the layout every
// DualSense tool uses, after Nielk1's trigger effect generator).
void haptics_trigger_off(std::uint8_t out[11]) {
    std::memset(out, 0, 11);
    out[0] = 0x05;
}

// Resistance from `start` to the end of the travel, 1..8.
void haptics_trigger_feedback(std::uint8_t out[11], int start, int strength) {
    if (start < 0 || start > 9 || strength < 1) return haptics_trigger_off(out);
    strength = std::min(strength, 8);
    std::memset(out, 0, 11);
    std::uint32_t force = 0;
    std::uint16_t active = 0;
    for (int i = start; i < 10; ++i) {
        force |= static_cast<std::uint32_t>(strength - 1) << (3 * i);
        active |= static_cast<std::uint16_t>(1u << i);
    }
    out[0] = 0x21;
    out[1] = static_cast<std::uint8_t>(active & 0xff);
    out[2] = static_cast<std::uint8_t>(active >> 8);
    for (int b = 0; b < 4; ++b) out[3 + b] = static_cast<std::uint8_t>(force >> (8 * b));
}

// A trigger break: resistance from `start` that gives way at `end`.
void haptics_trigger_weapon(std::uint8_t out[11], int start, int end, int strength) {
    if (start < 2 || start > 7 || end <= start || end > 8 || strength < 1) return haptics_trigger_off(out);
    strength = std::min(strength, 8);
    std::memset(out, 0, 11);
    const std::uint16_t zones = static_cast<std::uint16_t>((1u << start) | (1u << end));
    out[0] = 0x25;
    out[1] = static_cast<std::uint8_t>(zones & 0xff);
    out[2] = static_cast<std::uint8_t>(zones >> 8);
    out[3] = static_cast<std::uint8_t>(strength - 1);
}

// A hum from `position` to the end of the travel: amplitude 1..8, Hz.
void haptics_trigger_vibration(std::uint8_t out[11], int position, int amplitude, int frequency) {
    if (position < 0 || position > 9 || amplitude < 1 || frequency < 1) return haptics_trigger_off(out);
    amplitude = std::min(amplitude, 8);
    std::memset(out, 0, 11);
    std::uint32_t amps = 0;
    std::uint16_t active = 0;
    for (int i = position; i < 10; ++i) {
        amps |= static_cast<std::uint32_t>(amplitude - 1) << (3 * i);
        active |= static_cast<std::uint16_t>(1u << i);
    }
    out[0] = 0x26;
    out[1] = static_cast<std::uint8_t>(active & 0xff);
    out[2] = static_cast<std::uint8_t>(active >> 8);
    for (int b = 0; b < 4; ++b) out[3 + b] = static_cast<std::uint8_t>(amps >> (8 * b));
    out[9] = static_cast<std::uint8_t>(std::min(frequency, 255));
}


TriggerEffects haptics_triggers(std::uint32_t id, float l2, float r2, bool relaxed) {
    TriggerEffects t;
    const Wanted w = wanted();
    if (relaxed) g_gun_empty = false;
    if (!w.triggers || relaxed) {
        haptics_trigger_off(t.left);
        haptics_trigger_off(t.right);
        return t;
    }
    // Light, Medium, Strong.
    static constexpr int kResist[3] = {2, 3, 5}, kBreak[3] = {3, 5, 8}, kHum[3] = {4, 6, 8};
    const int s = std::clamp(w.trigger_strength, 0, 2);
    (void)l2;  // what the gun's trigger does follows the character, not how far it is pulled
    // What the character is doing (engine/world_chr.h): its animation, the
    // category's part off. Round 10: the hum followed R2 held, then stamina
    // spent - which the game spends at the swing, so the hum came after it.
    std::int32_t anim = -1;
    if (!world_player_anim(&anim)) anim = -1;
    const int motion = anim >= 0 ? anim % 1000000 : -1;
    const int category = anim >= 0 ? anim / 1000000 : -1;
    struct State {
        int motion = -1, category = -1;
        std::chrono::steady_clock::time_point since{};
        bool acted = false;  // an attack ran since R2 went down: free until it is let go
    };
    static std::unordered_map<std::uint32_t, State> states;  // the pump thread only
    State& st = states[id];
    const auto now = std::chrono::steady_clock::now();
    if (motion != st.motion || category != st.category) {
        static const bool log_anims = [] {
            const char* e = std::getenv("HELIAS_HAPTICS_ANIM_LOG");
            return e && e[0] == '1';
        }();
        if (log_anims) host_log("haptics: animation %d (category %d, %06d)", anim, category, motion);
        if (anim == 117054500 && !g_gun_empty) {
            g_gun_empty = true;
            host_log("haptics: the gun is empty - L2 slack until a shot, Blood Bullets, a menu or a load");
        } else if ((anim == 117051000 || anim == 100141) && g_gun_empty) {
            g_gun_empty = false;
            host_log("haptics: the gun has bullets again (%s)", anim == 100141 ? "Blood Bullets" : "a shot");
        }
        st.motion = motion;
        st.category = category;
        st.since = now;
    }
    const float in_ms = std::chrono::duration<float, std::milli>(now - st.since).count();

    // L2, the gun: a break two-fifths of the way down, and a recoil kick as
    // a shot's animation starts (category 117, the left hand's firearm; 054500
    // is the empty gun's, which fires nothing). Empty: slack, no kick.
    if (category == 117 && motion >= 50000 && motion < 60000 && motion != 54500 && in_ms < 120.0f) {
        haptics_trigger_vibration(t.left, 0, kHum[s], 60);
    } else if (gun_empty()) {
        haptics_trigger_off(t.left);
    } else {
        haptics_trigger_weapon(t.left, 3, 6, kBreak[s]);
    }
    // R2, the heavy attack, by what the character does (round 11):
    //   standing, moving, or an animation over: free travel, then a light wall
    //     where the attack registers (haptics_r2_fire_point) - felt before
    //     the attack, since the game moves only once the press is in;
    //   the wind-up (032600): that resistance;
    //   the charge (032610): a rumble growing over its half second, then
    //     full - fully charged;
    //   the charged swing (032630): one strong kick, then nothing;
    //   any other attack, and the rest of a swing: nothing at all.
    const bool standing = category == 200 || (motion >= 0 && motion < 10000) || anim < 0;
    // Round 12: "if an attack animation is played out the resistance should
    // not hold over" - R2 still held after an attack is free until let go.
    if (r2 < 0.15f) st.acted = false;
    else if (!standing) st.acted = true;
    if (motion == 32600) {
        haptics_trigger_feedback(t.right, 4, kResist[s] + 1);
    } else if (motion == 32610) {
        const float grow = std::clamp(in_ms / 450.0f, 0.0f, 1.0f);
        const int amp = 1 + static_cast<int>(std::lround(grow * static_cast<float>(kHum[s] - 1)));
        haptics_trigger_vibration(t.right, 0, amp, 30 + static_cast<int>(grow * 20.0f));
    } else if (motion == 32630 && in_ms < 120.0f) {
        haptics_trigger_vibration(t.right, 0, 8, 90);
    } else if (standing && !st.acted) {
        haptics_trigger_feedback(t.right, 4, kResist[s]);
    } else {
        haptics_trigger_off(t.right);
    }
    return t;
}
