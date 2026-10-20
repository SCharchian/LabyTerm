#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace laby {

// Decoded audio: 16-bit samples, interleaved when stereo.
struct Clip {
  std::vector<std::int16_t> pcm;
  int channels = 1;
  int rate = 22050;
  size_t frames() const { return pcm.size() / static_cast<size_t>(channels); }
};

// The original's "Bonk" (sound.lua sound:Bonk): a short triangle-wave note. `pitchOffsetHz` is how much higher than the
// base pitch: the original raised it as the player got nearer to the floor's secret door.
std::shared_ptr<Clip> synthBonk(int pitchOffsetHz, int rate = 44100);
// The sting that announces a monster's special action (action = MonsterAction as int): each has its own short sound.
std::shared_ptr<Clip> synthSting(int action, int rate = 44100);

// Mixes sound effects and one music track into stereo floats. No device, no SDL, no threads: AudioPlayer owns the lock.
// Sound effects overlap freely; music crossfades when the track changes and can remember where it was (the dungeon's
// does, as in the original, which paused that track and stopped the others).
class Mixer {
 public:
  explicit Mixer(int outRate) : outRate_(outRate) {}

  void play(std::shared_ptr<const Clip> clip, float gain, int delayFrames = 0);
  // Switches to this track (a different `id` than the current one; the same id is left alone). null = silence.
  void setMusic(int id, std::shared_ptr<const Clip> clip, float gain, bool keepPosition);
  void musicRun(bool run) { musicRunning_ = run; }  // pause / resume without losing the place
  void setMusicRate(float r) { musicRate_ = r; }
  void setVolumes(float master, float music, float sfx) { master_ = master; musicVol_ = music; sfxVol_ = sfx; }

  void mix(float* stereo, int frames);
  // Nothing is sounding or about to (so the device can sleep).
  bool idle() const;
  int musicId() const { return musicId_; }

 private:
  struct Voice {
    std::shared_ptr<const Clip> clip;
    double pos = 0;       // in the clip's own frames
    float gain = 1;       // track / effect level
    float env = 1, envTarget = 1, envStep = 0;  // fade, per output frame
    int delay = 0;        // output frames until it starts
    bool loop = false;
    bool dead() const { return !clip; }
  };
  void render(Voice& v, float vol, float rateMul, float* out, int frames) const;

  int outRate_;
  float master_ = 1, musicVol_ = 1, sfxVol_ = 1;
  std::vector<Voice> sfx_;
  Voice music_, fading_;
  int musicId_ = 0;
  bool musicKeep_ = false, musicRunning_ = true;
  float musicRate_ = 1;
  std::map<int, double> saved_;  // kept positions by track id
};

}  // namespace laby
