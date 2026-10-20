#include "audio/AudioPlayer.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <map>
#include <random>
#include <vector>

#include "audio/Mixer.hpp"
#include "core/DataFile.hpp"
#include "core/Enums.hpp"

namespace laby {
namespace {

constexpr int kOutRate = 48000;

struct SfxDef {
  std::vector<std::string> files;
  float volume = 1;
};
struct MusicDef {
  std::string file;
  float volume = 1;
  bool keepPosition = false;
};

}  // namespace

struct AudioPlayer::Impl {
  std::string dir;
  std::map<std::string, SfxDef> sfx;
  std::map<Music, MusicDef> music;
  std::map<std::string, std::shared_ptr<const Clip>> clips;  // by file name; null = tried and failed
  Mixer mixer{kOutRate};
  std::mt19937 rng{std::random_device{}()};  // variety only: never the game's seeded Rng
  SDL_AudioStream* stream = nullptr;
  bool muted = false, unfocused = false, running = false;
  Uint64 lastBusyMs = 0;
  std::vector<float> buf;

  static void SDLCALL callback(void* user, SDL_AudioStream* s, int additional, int) {
    auto* self = static_cast<Impl*>(user);  // runs with the stream locked
    const int frames = additional / static_cast<int>(2 * sizeof(float));
    if (frames <= 0) return;
    self->buf.resize(static_cast<size_t>(frames) * 2);
    self->mixer.mix(self->buf.data(), frames);
    SDL_PutAudioStreamData(s, self->buf.data(), frames * 2 * static_cast<int>(sizeof(float)));
  }

  std::shared_ptr<const Clip> clip(const std::string& name) {
    auto it = clips.find(name);
    if (it != clips.end()) return it->second;
    std::shared_ptr<const Clip> result;
    SDL_AudioSpec spec;
    Uint8* data = nullptr;
    Uint32 len = 0;
    if (SDL_LoadWAV((dir + "/" + name + ".wav").c_str(), &spec, &data, &len)) {
      SDL_AudioSpec want = spec;
      want.format = SDL_AUDIO_S16;
      Uint8* pcm = nullptr;
      int pcmLen = 0;
      if (SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len), &want, &pcm, &pcmLen)) {
        auto c = std::make_shared<Clip>();
        c->channels = want.channels;
        c->rate = want.freq;
        c->pcm.assign(reinterpret_cast<std::int16_t*>(pcm), reinterpret_cast<std::int16_t*>(pcm) + pcmLen / 2);
        result = c;
        SDL_free(pcm);
      }
      SDL_free(data);
    }
    if (!result) std::fprintf(stderr, "audio: cannot load %s.wav: %s\n", name.c_str(), SDL_GetError());
    return clips[name] = result;
  }

  // "AttackHit" + arg 1 -> "AttackHit.SHARP"; keys with no entry for the arg fall back to the bare name.
  const SfxDef* find(Sfx s, int arg) const {
    const std::string base(kSfxNames[static_cast<size_t>(s)]);
    std::string suffix;
    if (arg >= 0) {
      switch (s) {
        case Sfx::AttackHit: suffix = std::string(toString(static_cast<WeaponType>(arg))); break;
        case Sfx::MonsterAttack: case Sfx::MonsterDies: case Sfx::CharmFailed: case Sfx::CharmSuccess:
          suffix = std::string(toString(static_cast<MonsterClass>(arg)));
          break;
        case Sfx::PlayerHit: suffix = std::to_string(arg); break;
        default: break;
      }
    }
    if (!suffix.empty())
      if (auto it = sfx.find(base + "." + suffix); it != sfx.end()) return &it->second;
    if (auto it = sfx.find(base); it != sfx.end()) return &it->second;
    return nullptr;
  }

  void play(const AudioEvent& e) {
    const int delay = e.delayMs * kOutRate / 1000;
    if (e.sfx == Sfx::Bonk) {  // synthesized, not a file
      mixer.play(synthBonk(e.arg < 0 ? 0 : e.arg), 0.9375f, delay);
      return;
    }
    if (e.sfx == Sfx::MonsterSpecial) {
      mixer.play(synthSting(e.arg), 0.6f, delay);
      return;
    }
    const SfxDef* d = find(e.sfx, e.arg);
    if (!d || d->files.empty()) return;
    std::uniform_int_distribution<size_t> pick(0, d->files.size() - 1);
    mixer.play(clip(d->files[pick(rng)]), d->volume, delay);
  }

  void wake() {
    lastBusyMs = SDL_GetTicks();
    if (!running && !muted && !unfocused && stream) {
      SDL_ResumeAudioStreamDevice(stream);
      running = true;
    }
  }
};

AudioPlayer::AudioPlayer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

AudioPlayer::~AudioPlayer() {
  if (impl_ && impl_->stream) SDL_DestroyAudioStream(impl_->stream);  // closes the device; the callback is done after this
}

std::unique_ptr<AudioPlayer> AudioPlayer::create(const std::string& dataDir, std::string* why) {
  auto fail = [&](const std::string& m) {
    if (why) *why = m;
    return std::unique_ptr<AudioPlayer>();
  };
  auto impl = std::make_unique<Impl>();
  impl->dir = dataDir + "/audio";
  try {
    const auto j = loadDataFile("audio.json");
    for (auto& [k, v] : j.at("sfx").items()) {
      SfxDef d;
      d.files = v.at("files").get<std::vector<std::string>>();
      d.volume = v.value("volume", 1.0f);
      impl->sfx[k] = std::move(d);
    }
    for (auto& [k, v] : j.at("music").items()) {
      MusicDef d;
      d.file = v.at("file").get<std::string>();
      d.volume = v.value("volume", 1.0f);
      d.keepPosition = v.value("keepPosition", false);
      for (size_t i = 1; i < kMusicNames.size(); ++i)
        if (kMusicNames[i] == k) impl->music[static_cast<Music>(i)] = d;
    }
  } catch (const std::exception& e) {
    return fail(std::string("audio.json: ") + e.what());
  }
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return fail(std::string("no audio: ") + SDL_GetError());
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "2048");  // ~43 ms: a few wake-ups a second, not a few hundred
  const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kOutRate};
  impl->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Impl::callback, impl.get());
  if (!impl->stream) return fail(std::string("no audio device: ") + SDL_GetError());
  for (auto& [k, d] : impl->sfx)  // the sound effects are small: decode them all now so the first footstep doesn't hitch
    for (const std::string& f : d.files) impl->clip(f);
  auto p = std::unique_ptr<AudioPlayer>(new AudioPlayer(std::move(impl)));
  return p;
}

void AudioPlayer::handle(const AudioEvents& events) {
  if (events.empty() || !impl_->stream) return;
  impl_->wake();
  SDL_LockAudioStream(impl_->stream);
  for (const AudioEvent& e : events) {
    switch (e.kind) {
      case AudioEvent::Kind::Play: impl_->play(e); break;
      case AudioEvent::Kind::MusicRun: impl_->mixer.musicRun(true); break;
      case AudioEvent::Kind::MusicHold: impl_->mixer.musicRun(false); break;
      case AudioEvent::Kind::MusicRate: impl_->mixer.setMusicRate(static_cast<float>(e.arg) / 100); break;
      case AudioEvent::Kind::SetMusic: {
        auto it = impl_->music.find(e.music);
        if (e.music == Music::None || it == impl_->music.end()) {
          impl_->mixer.setMusic(static_cast<int>(e.music), nullptr, 1, false);
        } else {
          impl_->mixer.musicRun(true);
          impl_->mixer.setMusic(static_cast<int>(e.music), impl_->clip(it->second.file), it->second.volume, it->second.keepPosition);
        }
        break;
      }
    }
  }
  SDL_UnlockAudioStream(impl_->stream);
}

void AudioPlayer::setVolumes(double master, double music, double sfx) {
  if (!impl_->stream) return;
  SDL_LockAudioStream(impl_->stream);
  impl_->mixer.setVolumes(static_cast<float>(master), static_cast<float>(music), static_cast<float>(sfx));
  SDL_UnlockAudioStream(impl_->stream);
}

void AudioPlayer::setMuted(bool muted) {
  if (!impl_->stream || muted == impl_->muted) return;
  impl_->muted = muted;
  if (muted) {
    SDL_PauseAudioStreamDevice(impl_->stream);
    SDL_ClearAudioStream(impl_->stream);
    impl_->running = false;
  } else {
    impl_->wake();
  }
}

void AudioPlayer::setFocused(bool focused) {
  if (!impl_->stream || focused == !impl_->unfocused) return;
  impl_->unfocused = !focused;
  if (!focused) {  // frozen where it is (the mixer only advances in the device callback), so the music carries on when focus returns
    SDL_PauseAudioStreamDevice(impl_->stream);
    impl_->running = false;
  } else {
    impl_->wake();
  }
}

bool AudioPlayer::muted() const { return impl_->muted; }

void AudioPlayer::update() {
  if (!impl_->stream || !impl_->running) return;
  SDL_LockAudioStream(impl_->stream);
  const bool idle = impl_->mixer.idle();
  SDL_UnlockAudioStream(impl_->stream);
  if (!idle) {
    impl_->lastBusyMs = SDL_GetTicks();
  } else if (SDL_GetTicks() - impl_->lastBusyMs > 1500) {  // let the last sound's tail drain, then sleep
    SDL_PauseAudioStreamDevice(impl_->stream);
    impl_->running = false;
  }
}

}  // namespace laby
