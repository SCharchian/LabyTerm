#pragma once
#include <memory>
#include <string>

#include "audio/Cues.hpp"

namespace laby {

// Plays the game's AudioEvents on the default sound device (SDL3 audio stream + our Mixer). Sounds are the original's,
// stored as WAV files in data/audio/, and data/audio.json says which file makes which cue.
// Sounds that are missing or a missing device never stop the game: the player just stays quiet.
class AudioPlayer {
 public:
  // null (with the reason in *why) when there is no usable sound device or no audio.json.
  static std::unique_ptr<AudioPlayer> create(const std::string& dataDir, std::string* why);
  ~AudioPlayer();
  AudioPlayer(const AudioPlayer&) = delete;
  AudioPlayer& operator=(const AudioPlayer&) = delete;

  void handle(const AudioEvents& events);
  void setVolumes(double master, double music, double sfx);
  void setMuted(bool muted);
  bool muted() const;
  // While the window is not focused the device is paused (nothing plays, nothing advances); focus resumes it.
  void setFocused(bool focused);
  // Call now and then (about once a second is plenty): lets the device sleep while nothing is sounding.
  void update();

 private:
  struct Impl;
  explicit AudioPlayer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace laby
