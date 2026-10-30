#pragma once

// test11, HELIAS_PRESENT_PACE (on by default at a fixed pace: 30/60/90/120):
// presents go out on an even grid of the pace's period.
//
// t12 (test10, post at render on): the GPU was 84-88% busy at 120 fps, so it
// had ~1 ms to spare. Still, 39% of the frames MangoHud saw were over
// 8.42 ms. MangoHud times vkQueuePresentKHR calls, and a present follows its
// flip by the command processor's work on that frame plus the presenter's
// hand-over: 0.9 ms on average, up to 1.2-2 ms. The game's limiter keeps the
// flips even (its sleeps wake <60 us late at p99), so the spread is in
// between. Holding each present to its slot on a grid takes that spread out.
//
//   - The grid runs at the pace's period. A frame that arrives before its
//     slot waits for it (sleep, then spin; the presenter thread, no lock
//     held, after the blit is submitted).
//   - A frame that arrives after its slot goes out at once, and the grid
//     restarts from it. That makes one longer interval, never a short one
//     after it. The wait is never more than the time to the slot, so pacing
//     cannot make a frame later than one period after the last present.
//   - The grid drifts earlier when every frame of a window arrived well
//     ahead of its slot. That keeps the added latency to about the arrivals'
//     spread (a few hundred us), at most kCreepNs a frame.
//   - rc2: vk1-dream's grid sat 3-4 ms behind the arrivals for seconds after
//     every late frame (20 us a frame, decided every 120 frames: a 4 ms
//     excess took ~200 frames to win back). Now decided every 30 frames and
//     up to 100 us a frame: the same excess is gone in ~40 frames (1/3 s).
//   - Under the pace (GPU-bound), every frame is late, so they pass straight
//     through, as before.
//   - rc3 (rc2's hardware run: the pacer waited ~2.3 ms a frame on average,
//     flip to present 5-7 ms): one late frame zeroed its window's earliest
//     arrival, so the creep never moved; the window is now judged on its
//     3rd-earliest early frame and late frames only stop the creep when they
//     are a quarter of the window (GPU-bound). A frame that is far ahead of
//     its slot (over kCapNs, 1 ms) right after an on-time present pulls the grid
//     in by up to kPullNs at once - an interval at most 0.5 ms short, never
//     a long one. presented() takes the time just before the present call
//     (window.cpp); a present that went after its slot (the blit's submit
//     was late) restarts the grid there, as a late arrival does.
// Pure logic, no clocks (tests/present_pacer_test.cpp).

#include <algorithm>
#include <cstdint>

namespace temporal {

class PresentPacer {
public:
    static constexpr std::int64_t kCreepNs = 100000;     // the grid's earliest move a frame (the window's decision)
    static constexpr std::int64_t kKeepNs = 150000;      // the margin the robust-earliest arrival keeps
    static constexpr int kWindow = 30;                   // frames between creep decisions
    static constexpr int kRobust = 2;                    // the window is judged on its (kRobust+1)-th earliest early frame
    static constexpr std::int64_t kCapNs = 1000000;      // rc3: a wait past this right after an on-time present ...
    static constexpr std::int64_t kPullNs = 500000;      // ... pulls the grid in by up to this, at once
    static constexpr std::int64_t kLateSlackNs = 50000;  // presented() this far past the slot counts as late

    void set_period(std::int64_t ns) {
        if (ns != period_) {
            period_ = ns;
            next_ = 0;
        }
    }
    std::int64_t period() const { return period_; }

    // A frame ready to present at `now`: how long to wait (0: at once).
    // Then call presented() with when it went.
    std::int64_t wait_ns(std::int64_t now) {
        if (period_ <= 0) return 0;
        if (!next_ || now >= next_ + period_ || now < next_ - period_) {  // first, or far off the grid
            next_ = now;
            ++reanchors_;
            reset_window();
            last_on_time_ = false;
            return 0;
        }
        if (now >= next_) {  // late for its slot: at once, the grid restarts here
            ++late_;
            ++window_late_;
            next_ = now;
            last_on_time_ = false;
            return 0;
        }
        std::int64_t early = next_ - now;
        if (last_on_time_ && early > kCapNs) {  // the grid lags the arrivals: pull it in
            const std::int64_t pull = std::min(early - kCapNs, kPullNs);
            next_ -= pull;
            early -= pull;
            pulled_total_ += pull;
            ++pulls_;
        }
        if (window_n_ < kWindow) window_early_[window_n_++] = early;
        ++waited_;
        wait_total_ += early;
        last_on_time_ = true;
        return early;
    }

    // The frame went at `t` (normally the slot; rc3: taken just before the
    // present call): the next slot is a period on, pulled earlier when the
    // last window's frames were well ahead.
    void presented(std::int64_t t) {
        if (period_ <= 0) return;
        if (t > next_ + kLateSlackNs) {  // went after its slot (late arrival, or the submit held it)
            if (last_on_time_) {
                ++late_present_;
                ++window_late_;
            }
            last_on_time_ = false;
        }
        if (t > next_) next_ = t;
        std::int64_t step = period_;
        if (++in_window_ >= kWindow) {
            decide();
            in_window_ = 0;
        }
        if (creep_) {
            step -= creep_;
            creep_total_ += creep_;
        }
        next_ += step;
    }

    std::uint64_t reanchors() const { return reanchors_; }
    std::uint64_t late() const { return late_; }
    std::uint64_t late_presents() const { return late_present_; }
    std::uint64_t waited() const { return waited_; }
    std::uint64_t pulls() const { return pulls_; }
    std::int64_t wait_total_ns() const { return wait_total_; }
    std::int64_t creep_total_ns() const { return creep_total_; }
    std::int64_t pulled_total_ns() const { return pulled_total_; }
    std::int64_t creep_ns() const { return creep_; }
    std::int64_t last_robust_early_ns() const { return last_robust_; }  // -1: the last window had too few early frames

private:
    void reset_window() {
        window_n_ = 0;
        window_late_ = 0;
    }
    void decide() {
        // Too many late frames (GPU-bound, or the grid is already at the
        // arrivals): no creep. A few late ones are ignored.
        if (window_late_ * 4 > kWindow || window_n_ <= kRobust) {
            creep_ = 0;
            last_robust_ = -1;
        } else {
            std::int64_t v[kWindow];
            std::copy(window_early_, window_early_ + window_n_, v);
            std::nth_element(v, v + kRobust, v + window_n_);
            last_robust_ = v[kRobust];
            creep_ = last_robust_ > kKeepNs ? std::min<std::int64_t>((last_robust_ - kKeepNs) / kWindow + 1, kCreepNs) : 0;
        }
        reset_window();
    }

    std::int64_t period_ = 0;
    std::int64_t next_ = 0;  // the next slot; 0: none yet
    std::int64_t window_early_[kWindow] = {};
    int window_n_ = 0, window_late_ = 0;
    std::int64_t creep_ = 0, last_robust_ = -1;
    int in_window_ = 0;
    bool last_on_time_ = false;
    std::uint64_t reanchors_ = 0, late_ = 0, late_present_ = 0, waited_ = 0, pulls_ = 0;
    std::int64_t wait_total_ = 0, creep_total_ = 0, pulled_total_ = 0;
};

}  // namespace temporal
