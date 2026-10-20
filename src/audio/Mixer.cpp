#include "audio/Mixer.hpp"

#include <algorithm>
#include <cmath>

namespace laby {
namespace {

constexpr float kFadeSeconds = 0.15f;
constexpr size_t kMaxSfx = 48;

// Keeps a loud sum from clipping harshly: linear up to 0.8, then a smooth knee towards 1.
float limit(float x) {
  const float a = std::fabs(x);
  if (a <= 0.8f) return x;
  const float y = 0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f);
  return x < 0 ? -y : y;
}

}  // namespace

std::shared_ptr<Clip> synthBonk(int pitchOffsetHz, int rate) {
  // sound.lua: a triangle synth, ADSR 0.01 / 0.2 / 1 / 0.05, and the last of its three notes is the one that sounds
  // (a Playdate synth is monophonic): 143 Hz + pitch, volume 0.9, 0.14 s long.
  const double hz = 143.0 + pitchOffsetHz, len = 0.14, attack = 0.01, release = 0.05, vol = 0.9;
  auto c = std::make_shared<Clip>();
  c->rate = rate;
  const int n = static_cast<int>((len + release) * rate);
  c->pcm.resize(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / rate;
    const double ph = std::fmod(t * hz, 1.0);
    const double tri = 4 * std::fabs(ph - 0.5) - 1;
    double env = std::min(1.0, t / attack);
    if (t > len) env *= std::max(0.0, 1 - (t - len) / release);
    c->pcm[static_cast<size_t>(i)] = static_cast<std::int16_t>(tri * env * vol * 0.6 * 32767);
  }
  return c;
}

std::shared_ptr<Clip> synthSting(int action, int rate) {
  // 0 attack (unused) 1 drain: a low saw sinking  2 bite: a sharp noise snap  3 fire: a noise roar rising  4 lull: two soft sine notes  5 dust: a hiss
  struct Def { double hz0, hz1, len, noise; int wave; };  // wave: 0 sine, 1 saw, 2 square
  static const Def defs[] = {{220, 220, 0.2, 0, 0}, {330, 110, 0.38, 0.05, 1}, {500, 300, 0.16, 0.7, 2}, {150, 600, 0.42, 0.55, 1}, {523, 392, 0.5, 0, 0}, {900, 2000, 0.3, 0.9, 0}};
  const Def& d = defs[std::clamp(action, 0, 5)];
  auto c = std::make_shared<Clip>();
  c->rate = rate;
  const int n = static_cast<int>((d.len + 0.05) * rate);
  c->pcm.resize(static_cast<size_t>(n));
  std::uint32_t lcg = 12345u + static_cast<std::uint32_t>(action) * 777u;
  double phase = 0;
  for (int i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / rate, u = std::min(1.0, t / d.len);
    double hz = d.hz0 + (d.hz1 - d.hz0) * u;
    if (action == 4) hz = t < d.len / 2 ? d.hz0 : d.hz1;  // the lullaby: two notes
    phase = std::fmod(phase + hz / rate, 1.0);
    const double tone = d.wave == 0 ? std::sin(phase * 6.283185307) : d.wave == 1 ? 2 * phase - 1 : (phase < 0.5 ? 1.0 : -1.0);
    lcg = lcg * 1664525u + 1013904223u;
    const double noise = (static_cast<double>(lcg >> 8) / 8388608.0) - 1;
    double env = std::min(1.0, t / 0.008) * (action == 4 ? 1 - 0.5 * u : 1 - u * u);
    if (t > d.len) env *= std::max(0.0, 1 - (t - d.len) / 0.05);
    c->pcm[static_cast<size_t>(i)] = static_cast<std::int16_t>((tone * (1 - d.noise) + noise * d.noise) * env * 0.5 * 32767);
  }
  return c;
}

void Mixer::play(std::shared_ptr<const Clip> clip, float gain, int delayFrames) {
  if (!clip || clip->frames() == 0) return;
  if (sfx_.size() >= kMaxSfx) sfx_.erase(sfx_.begin());  // drop the oldest rather than pile up
  Voice v;
  v.clip = std::move(clip);
  v.gain = gain;
  v.delay = delayFrames;
  sfx_.push_back(std::move(v));
}

void Mixer::setMusic(int id, std::shared_ptr<const Clip> clip, float gain, bool keepPosition) {
  if (id == musicId_ && (music_.clip || !clip)) return;
  if (music_.clip) {  // the old track fades out
    if (musicKeep_) saved_[musicId_] = music_.pos;
    fading_ = std::move(music_);
    fading_.envTarget = 0;
    fading_.envStep = -fading_.env / (kFadeSeconds * outRate_);
  }
  music_ = Voice();
  musicId_ = id;
  musicKeep_ = keepPosition;
  musicRate_ = 1;
  if (!clip) return;
  music_.clip = std::move(clip);
  music_.gain = gain;
  music_.loop = true;
  music_.env = 0;
  music_.envTarget = 1;
  music_.envStep = 1 / (kFadeSeconds * outRate_);
  if (keepPosition) {
    auto it = saved_.find(id);
    if (it != saved_.end()) music_.pos = it->second;
  }
}

bool Mixer::idle() const { return sfx_.empty() && !fading_.clip && !(music_.clip && musicRunning_ && musicVol_ * master_ > 0); }

void Mixer::render(Voice& v, float vol, float rateMul, float* out, int frames) const {
  const Clip& c = *v.clip;
  const double step = static_cast<double>(c.rate) / outRate_ * rateMul;
  const size_t n = c.frames();
  const std::int16_t* p = c.pcm.data();
  const size_t ch = static_cast<size_t>(c.channels);
  for (int i = 0; i < frames && v.clip; ++i) {
    if (v.delay > 0) {
      --v.delay;
      continue;
    }
    if (v.envStep != 0) {
      v.env += v.envStep;
      if ((v.envStep > 0 && v.env >= v.envTarget) || (v.envStep < 0 && v.env <= v.envTarget)) {
        v.env = v.envTarget;
        v.envStep = 0;
        if (v.envTarget == 0) v.clip.reset();
      }
    }
    if (!v.clip) break;
    size_t i0 = static_cast<size_t>(v.pos);
    if (i0 >= n) {
      if (!v.loop) {
        v.clip.reset();
        break;
      }
      v.pos = std::fmod(v.pos, static_cast<double>(n));
      i0 = static_cast<size_t>(v.pos);
    }
    size_t i1 = i0 + 1;
    if (i1 >= n) i1 = v.loop ? 0 : i0;
    const float f = static_cast<float>(v.pos - static_cast<double>(i0));
    const float g = vol * v.gain * v.env * (1.0f / 32768);
    float l = (p[i0 * ch] * (1 - f) + p[i1 * ch] * f) * g;
    float r = ch == 2 ? (p[i0 * ch + 1] * (1 - f) + p[i1 * ch + 1] * f) * g : l;
    out[2 * i] += l;
    out[2 * i + 1] += r;
    v.pos += step;
  }
}

void Mixer::mix(float* out, int frames) {
  std::fill(out, out + 2 * frames, 0.0f);
  if (fading_.clip) render(fading_, musicVol_ * master_, 1, out, frames);
  if (music_.clip && musicRunning_) render(music_, musicVol_ * master_, musicRate_, out, frames);
  for (Voice& v : sfx_) render(v, sfxVol_ * master_, 1, out, frames);
  sfx_.erase(std::remove_if(sfx_.begin(), sfx_.end(), [](const Voice& v) { return v.dead(); }), sfx_.end());
  for (int i = 0; i < 2 * frames; ++i) out[i] = limit(out[i]);
}

}  // namespace laby
