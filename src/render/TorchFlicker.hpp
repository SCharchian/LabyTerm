#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

#include "core/Tuning.hpp"

namespace laby {

// The torch's flicker: at random (about every `torch.flickerSeconds`) the light swings for a few frames, as in the
// original's DoTorch (a horizontal flip of the darkness picture for 2-5 frames, at 30 frames a second). Time only moves
// when update() is called, and the app needs to wake only for the next start (`msUntilNext`) and, during a flicker,
// at its frame rate: an idle window costs nothing. While the window is not focused (setActive(false)) it does not run.
class TorchFlicker {
 public:
  void setActive(bool on) {
    if (on == active_) return;
    active_ = on;
    nextStart_ = 0;
    frames_ = 0;
    frame_ = -1;
  }
  bool active() const { return active_; }

  // Advances to `now`. Returns true if what is drawn changed (a flicker frame began or the flicker ended).
  // `crackle` is set when a flicker has just begun (play the sound).
  bool update(std::uint64_t now, std::mt19937& rng, bool& crackle) {
    const auto& k = tuning().torch;
    crackle = false;
    const int before = frame_;
    bool changed = false;
    if (!active_ || !k.enabled) {
      nextStart_ = 0;
      frames_ = 0;
      frame_ = -1;
      return before != -1;
    }
    if (const int tick = breathTick(now); tick != breathTick_) {
      breathTick_ = tick;
      changed = true;
    }
    if (k.flickerSeconds <= 0) {
      frames_ = 0;
      frame_ = -1;
      return changed || before != -1;
    }
    if (nextStart_ == 0) schedule(now, rng);
    if (frames_ == 0 && now >= nextStart_) {
      const int lo = std::max(1, k.flickerMinFrames), hi = std::max(lo, k.flickerMaxFrames);
      frames_ = std::uniform_int_distribution<int>(lo, hi)(rng);
      start_ = now;
      parity_ = static_cast<int>(rng() & 1);
      crackle = true;
    }
    if (frames_ > 0) {
      const int f = static_cast<int>((now - start_) * static_cast<std::uint64_t>(std::max(1, k.flickerFps)) / 1000);
      if (f >= frames_) {
        frames_ = 0;
        frame_ = -1;
        schedule(now, rng);
      } else {
        frame_ = f;
      }
    }
    return changed || frame_ != before;
  }

  // Milliseconds until update() next has something to do (a huge number when it never will).
  std::uint64_t msUntilNext(std::uint64_t now) const {
    const auto& k = tuning().torch;
    if (!active_ || !k.enabled) return 1u << 30;
    std::uint64_t next = k.flickerSeconds <= 0 ? 1u << 30 : nextStart_ > now ? nextStart_ - now : 0;
    if (frames_ > 0) next = 1000u / static_cast<std::uint64_t>(std::max(1, k.flickerFps)) / 2 + 1;
    if (k.breathSeconds > 0 && k.breathAmount > 0) {  // the next redraw of the breathing
      const std::uint64_t step = 1000u / static_cast<std::uint64_t>(std::max(1, k.breathFps));
      next = std::min(next, step - now % step);
    }
    return next;
  }

  // The calm torch's breathing: the light's radius offset, two slow waves of different lengths so it does not look like a metronome.
  // Held steady between redraws (breathFps) so the picture only changes when it is drawn again.
  float breath() const {
    const auto& k = tuning().torch;
    if (!active_ || k.breathSeconds <= 0 || k.breathAmount <= 0) return 0.0f;
    const double t = breathTick_ * (1.0 / std::max(1, k.breathFps)), w = 6.283185307179586 / k.breathSeconds;
    return static_cast<float>(k.breathAmount * (0.7 * std::sin(w * t) + 0.3 * std::sin(w * t * 2.3 + 1.0)));
  }
  bool flickering() const { return frame_ >= 0; }
  // The current frame's sideways swing and darkness change (alternating each frame; both 0 when calm).
  float shift() const { return frame_ < 0 ? 0.0f : static_cast<float>(((frame_ + parity_) & 1) ? 1 : -1) * static_cast<float>(tuning().torch.flickerShift); }
  float pulse() const { return frame_ < 0 ? 0.0f : static_cast<float>(((frame_ + parity_) & 1) ? 1 : -1) * static_cast<float>(tuning().torch.flickerPulse); }
  // For tests and previews: show flicker frame n (1, 2, ...; 0 = calm) without a clock.
  void forceBreath(double seconds) { breathTick_ = static_cast<int>(seconds * std::max(1, tuning().torch.breathFps)); }
  void force(int n) { frame_ = n - 1; parity_ = 0; frames_ = 0; }

 private:
  void schedule(std::uint64_t now, std::mt19937& rng) {
    const double every = tuning().torch.flickerSeconds;
    nextStart_ = now + 1 + static_cast<std::uint64_t>(std::exponential_distribution<double>(1.0 / every)(rng) * 1000);
  }
  static int breathTick(std::uint64_t now) { return static_cast<int>(now * static_cast<std::uint64_t>(std::max(1, tuning().torch.breathFps)) / 1000); }
  bool active_ = true;
  int breathTick_ = 0;
  std::uint64_t nextStart_ = 0, start_ = 0;
  int frames_ = 0, frame_ = -1, parity_ = 0;
};

}  // namespace laby
