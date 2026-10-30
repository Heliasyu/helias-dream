#pragma once

// test8, HELIAS_MOTION_BLUR_SKIP=1: leaving out the motion blur's two draws a
// frame (YEBIS e0305cef, ~0.29-0.33 ms at 4K) while motion blur is off.
//
// With the setting off, shader_patch.cpp makes the blur's compare always
// false, so each draw writes its colour input through unchanged: draw 1
// copies S into B, draw 2 copies B into D. In the game (t10's post-chain
// log) S is the depth of field's output 0x165e30000, B 0x169e00000 and D the
// chain's colour 0x161e60000. Leaving both out and copying S into D gives
// the same D exactly, as long as nothing else uses B (it keeps an older
// picture). One copy instead of two draws; when S is D (a ping-pong), not
// even that. test8 only took S == D and so never skipped (t10: 0 pairs).
// That is what this policy proves before it skips anything, and keeps
// checking while it does:
//   - learning: a frame counts only if its motion-blur draws were exactly
//     (S -> B, B -> D), with no blending, and nothing else read or wrote B
//     that frame (draws, dispatches, copies; render.cpp reports them).
//     kLearnFrames such frames in a row, the same S, B and D, arm it.
//   - armed: draw 1 matching (S -> B) is skipped and remembered; draw 2
//     matching (B -> D) is skipped too, and the caller copies S into D
//     (SkipCopy; Skip when S is D). Anything else first - another draw, a
//     dispatch, a copy, or a second draw that does not match - makes draw 1
//     happen after all (Materialize: the caller copies S into B, which is
//     what the patched draw writes) and disarms.
//   - B read or written by anything else while armed: disarm (a trip,
//     logged) and learn again. A new size, new targets or the setting on: the
//     pattern no longer matches, and nothing is skipped.
// Pure logic, no Vulkan (tests/mblur_skip_test.cpp).

#include <cstdint>

namespace temporal {

class MotionBlurSkip {
public:
    static constexpr int kLearnFrames = 120;

    enum class Action { Draw, Skip, SkipCopy, MaterializeThenDraw };

    // One of the motion blur's draws: its colour input and target, whether it
    // blends, and the flip it is in. `eligible`: the skip is on, motion blur
    // is off, and the caller found one colour input of the target's size and
    // format.
    Action draw(std::uint64_t flip, std::uint64_t src, std::uint64_t dst, bool blends, bool eligible) {
        const bool held = new_frame(flip);
        const int idx = frame_draws_++;
        if (idx == 0) {
            d1_src_ = src;
            d1_dst_ = dst;
        } else if (idx == 1) {
            d2_src_ = src;
            d2_dst_ = dst;
        }
        frame_blends_ |= blends;
        frame_ineligible_ |= !eligible;
        if (held) {  // draw 1 of the last frame never got done: first that, then this one as it comes
            trip();
            ++materialized_;
            return Action::MaterializeThenDraw;
        }
        if (!eligible || blends) return disarm_pending(Action::Draw);
        if (!armed_) return Action::Draw;
        if (idx == 0 && src == a_ && dst == b_) {
            pending_ = true;
            return Action::Skip;
        }
        if (idx == 1 && pending_ && src == b_ && dst == d_) {
            pending_ = false;
            ++pairs_skipped_;
            return d_ == a_ ? Action::Skip : Action::SkipCopy;
        }
        return disarm_pending(Action::Draw);
    }

    // Something else is about to run (a draw, a dispatch, a copy) while draw 1
    // is held back: it must be done first. True: the caller copies A into B.
    bool other_work() {
        if (!pending_) return false;
        pending_ = false;
        trip();
        ++materialized_;
        return true;
    }

    // Something other than the motion blur read or wrote B.
    void b_touched(std::uint64_t base) {
        if (!b_ || base != b_) return;
        frame_touched_ = true;
        if (armed_) trip();
    }

    std::uint64_t a() const { return a_; }  // S, draw 1's input
    std::uint64_t b() const { return b_; }  // B, the scratch between the draws
    std::uint64_t d() const { return d_; }  // D, draw 2's target
    // The scratch target the policy watches (learning or armed), else 0.
    std::uint64_t watched() const { return b_; }
    bool armed() const { return armed_; }
    bool pending() const { return pending_; }
    std::uint64_t pairs_skipped() const { return pairs_skipped_; }
    std::uint64_t trips() const { return trips_; }
    std::uint64_t materialized() const { return materialized_; }
    std::uint64_t arms() const { return arms_; }
    int good_frames() const { return good_; }
    // Why the last frame judged did not count ("" when it did).
    const char* last_reject() const { return reject_; }

private:
    // True: the last frame ended with draw 1 still held back.
    bool new_frame(std::uint64_t flip) {
        if (flip == flip_) return false;
        const bool held = pending_;
        if (flip_ != ~0ull) judge();
        flip_ = flip;
        frame_draws_ = 0;
        frame_blends_ = frame_ineligible_ = frame_touched_ = false;
        d1_src_ = d1_dst_ = d2_src_ = d2_dst_ = 0;
        pending_ = false;  // a frame that ended with draw 1 held was judged broken above
        return held;
    }
    // The finished frame: was it the pattern?
    void judge() {
        const bool ok = frame_draws_ == 2 && !frame_blends_ && !frame_ineligible_ && !frame_touched_ && d1_src_ && d1_dst_ &&
                        d1_src_ != d1_dst_ && d2_src_ == d1_dst_ && d2_dst_ && d2_dst_ != d1_dst_;
        reject_ = frame_draws_ != 2 ? "not two draws" : frame_blends_ ? "blending" : frame_ineligible_ ? "not eligible (blur on, or no single colour input)"
                  : frame_touched_ ? "B used elsewhere" : !(d1_src_ && d1_dst_ && d1_src_ != d1_dst_ && d2_src_ == d1_dst_ && d2_dst_ && d2_dst_ != d1_dst_) ? "not S->B, B->D"
                  : "";
        if (!ok) {
            if (armed_) trip();
            good_ = 0;
            return;
        }
        if (d1_src_ != a_ || d1_dst_ != b_ || d2_dst_ != d_) {
            if (armed_) trip();
            a_ = d1_src_;
            b_ = d1_dst_;
            d_ = d2_dst_;
            good_ = 1;
            return;
        }
        if (!armed_ && ++good_ >= kLearnFrames) {
            armed_ = true;
            ++arms_;
        }
    }
    void trip() {
        if (armed_) ++trips_;
        armed_ = false;
        good_ = 0;
    }
    Action disarm_pending(Action normal) {
        if (pending_) {
            pending_ = false;
            trip();
            ++materialized_;
            return Action::MaterializeThenDraw;
        }
        if (armed_) trip();
        return normal;
    }

    std::uint64_t flip_ = ~0ull;
    int frame_draws_ = 0;
    bool frame_blends_ = false, frame_ineligible_ = false, frame_touched_ = false;
    std::uint64_t d1_src_ = 0, d1_dst_ = 0, d2_src_ = 0, d2_dst_ = 0;
    std::uint64_t a_ = 0, b_ = 0, d_ = 0;
    const char* reject_ = "";
    int good_ = 0;
    bool armed_ = false, pending_ = false;
    std::uint64_t pairs_skipped_ = 0, trips_ = 0, materialized_ = 0, arms_ = 0;
};

}  // namespace temporal
