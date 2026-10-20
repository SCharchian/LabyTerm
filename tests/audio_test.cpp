// Sound checks without a sound device: every converted file decodes to real audio, the Mixer behaves, the sound map
// (data/audio.json) only names files that exist, and a game session reports the sounds the original played.
#include <SDL3/SDL.h>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <filesystem>
#include <set>

#include "audio/Mixer.hpp"
#include "core/DataFile.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;

static int failures = 0;
#define CHECK(c, ...)                                      \
  do {                                                     \
    if (!(c)) {                                            \
      ++failures;                                          \
      std::printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #c); \
      std::printf(__VA_ARGS__);                            \
      std::printf("\n");                                   \
    }                                                      \
  } while (0)

static std::shared_ptr<Clip> load(const std::string& path) {
  SDL_AudioSpec spec, want;
  Uint8* data = nullptr;
  Uint32 len = 0;
  if (!SDL_LoadWAV(path.c_str(), &spec, &data, &len)) return nullptr;
  want = spec;
  want.format = SDL_AUDIO_S16;
  Uint8* pcm = nullptr;
  int n = 0;
  if (!SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len), &want, &pcm, &n)) return nullptr;
  auto c = std::make_shared<Clip>();
  c->channels = want.channels;
  c->rate = want.freq;
  c->pcm.assign(reinterpret_cast<std::int16_t*>(pcm), reinterpret_cast<std::int16_t*>(pcm) + n / 2);
  SDL_free(pcm);
  SDL_free(data);
  return c;
}

static void testFiles() {
  const std::string dir = dataDir() + "/audio";
  int n = 0;
  for (auto& e : std::filesystem::directory_iterator(dir)) {
    if (e.path().extension() != ".wav") continue;
    ++n;
    auto c = load(e.path().string());
    CHECK(c && c->frames() > 100, "%s did not decode", e.path().c_str());
    if (!c) continue;
    const size_t ch = static_cast<size_t>(c->channels);
    const std::string stem = e.path().stem().string();

    // Pops: a right decode continues smoothly across ADPCM block boundaries (each block's header restarts the predictor),
    // a wrong nibble/channel layout jumps there. (Right: about 1x a normal sample step, up to ~4x on tiny transient sounds;
    // wrong: 6x on mono, 50x+ on stereo.)
    std::ifstream f(e.path(), std::ios::binary);
    std::vector<unsigned char> head(40);
    f.read(reinterpret_cast<char*>(head.data()), 40);
    const size_t perBlock = head[38] | (head[39] << 8);  // the fmt chunk's samples per block
    if (perBlock > 8 && c->frames() > perBlock * 6) {
      std::vector<double> edges, steps;
      for (size_t fr = 1; fr < c->frames() && steps.size() < 200000; ++fr) {
        const double step = std::fabs(static_cast<double>(c->pcm[fr * ch]) - c->pcm[(fr - 1) * ch]);
        (fr % perBlock == 0 ? edges : steps).push_back(step);
      }
      std::sort(edges.begin(), edges.end());
      std::sort(steps.begin(), steps.end());
      const double ratio = edges[edges.size() / 2] / (steps[steps.size() / 2] + 1.0);
      CHECK(ratio < 6.0, "%s pops at block boundaries (median jump %.1fx a normal step)", e.path().c_str(), ratio);
    }

    // The music: the right channel count, and (stereo tracks) no static.
    static const std::map<std::string, int> kMusicChannels = {
        {"Eerie_Dungeon", 2}, {"SplashScreen_RAIN", 2}, {"Combat_Music", 1}, {"Boss_Combat_Music", 1}, {"CharCreation_Storm", 1},
        {"ELEVATOR_Music", 1}, {"TOWN_RoadSign", 1}, {"TOWN_Temple", 1}, {"TOWN_Blacksmith", 1}, {"TOWN_BeastMart", 1},
        {"TownPortal_IDLE", 1}, {"Secret_Opening", 1}, {"InstantOrbOrbit", 1}};
    if (auto m = kMusicChannels.find(stem); m != kMusicChannels.end()) {
      CHECK(c->channels == m->second, "%s should have %d channel(s), has %d", stem.c_str(), m->second, c->channels);
      // (only for the stereo tracks, where a wrong layout gave 30-50x the static of a right one; bright mono tracks like the blacksmith's anvil
      // legitimately measure high, and their layout is covered by the pop check above)
      if (m->second != 2) continue;
      double d2 = 0, energy = 1e-9;
      for (size_t i = ch; i + ch < c->pcm.size(); ++i) {
        const double v = static_cast<double>(c->pcm[i + ch]) - 2.0 * c->pcm[i] + c->pcm[i - ch];
        d2 += v * v;
        energy += static_cast<double>(c->pcm[i]) * c->pcm[i];
      }
      CHECK(d2 / energy < 0.05, "%s has static in it (hf ratio %.3f)", stem.c_str(), d2 / energy);
    }
  }
  CHECK(n == 111, "expected 111 converted sounds, found %d", n);
}

static void testMap() {
  const auto j = loadDataFile("audio.json");
  std::set<std::string> used;
  for (auto& [k, v] : j["sfx"].items()) {
    bool known = false;
    for (auto name : kSfxNames) known |= k == name || k.rfind(std::string(name) + ".", 0) == 0;
    CHECK(known, "audio.json: %s is not a cue name", k.c_str());
    for (auto& f : v["files"]) {
      used.insert(f.get<std::string>());
      CHECK(std::filesystem::exists(dataDir() + "/audio/" + f.get<std::string>() + ".wav"), "%s missing", f.get<std::string>().c_str());
    }
  }
  for (auto& [k, v] : j["music"].items()) {
    bool known = false;
    for (auto name : kMusicNames) known |= k == name;
    CHECK(known, "audio.json: %s is not a music name", k.c_str());
    used.insert(v["file"].get<std::string>());
    CHECK(std::filesystem::exists(dataDir() + "/audio/" + v["file"].get<std::string>() + ".wav"), "%s missing", k.c_str());
  }
  CHECK(used.size() >= 100, "only %zu files are used", used.size());
  for (auto name : kSfxNames)
    if (name != "Bonk" && name != "MonsterSpecial") {  // (synthesized)
      bool has = false;
      for (auto& [k, v] : j["sfx"].items()) has |= k == name || k.rfind(std::string(name) + ".", 0) == 0;
      CHECK(has, "no sound for %s", std::string(name).c_str());
    }
}

static float peak(const std::vector<float>& v) {
  float p = 0;
  for (float x : v) p = std::max(p, std::fabs(x));
  return p;
}

static void testMixer() {
  Mixer m(48000);
  CHECK(m.idle(), "new mixer should be idle");
  std::vector<float> out(2 * 4800);
  for (int a = 1; a <= 5; ++a) {
    auto st = synthSting(a);
    CHECK(st->frames() > 4000, "sting %d is empty", a);
  }
  auto bonk = synthBonk(20);
  CHECK(bonk->frames() > 8000, "bonk too short");
  m.play(bonk, 1.0f, 2400);  // 50 ms late
  m.mix(out.data(), 2400);
  CHECK(peak(out) == 0, "a delayed sound started early");
  CHECK(!m.idle(), "mixer should not be idle with a sound pending");
  m.mix(out.data(), 4800);
  CHECK(peak(out) > 0.1f && peak(out) <= 1.0f, "bonk peak %.2f", peak(out));
  for (int i = 0; i < 20; ++i) m.mix(out.data(), 4800);
  CHECK(m.idle(), "mixer should go idle after the sound ended");

  // many loud overlapping sounds never exceed full scale
  auto loud = std::make_shared<Clip>();
  loud->pcm.assign(48000, 32000);
  for (int i = 0; i < 10; ++i) m.play(loud, 1.0f);
  m.mix(out.data(), 4800);
  CHECK(peak(out) <= 1.0f, "limiter let %.2f through", peak(out));

  // music: loops, crossfades, remembers its place when asked, can be held
  Mixer mm(48000);
  auto ramp = std::make_shared<Clip>();
  ramp->rate = 48000;
  ramp->pcm.resize(48000);
  for (size_t i = 0; i < ramp->pcm.size(); ++i) ramp->pcm[i] = static_cast<std::int16_t>(10000 * std::sin(i * 0.05));
  mm.setMusic(1, ramp, 1.0f, true);
  for (int i = 0; i < 30; ++i) mm.mix(out.data(), 4800);  // 3 s: wraps the 1 s clip
  CHECK(peak(out) > 0.1f, "looping music went silent");
  mm.musicRun(false);
  mm.mix(out.data(), 4800);
  CHECK(peak(out) == 0 && mm.idle(), "held music still sounds");
  mm.setMusic(2, ramp, 1.0f, false);
  mm.setMusic(1, ramp, 1.0f, true);  // back to track 1: continues where it left (position kept), no crash
  mm.musicRun(true);
  for (int i = 0; i < 20; ++i) mm.mix(out.data(), 4800);
  CHECK(mm.musicId() == 1 && peak(out) > 0.1f, "music switch");
  mm.setMusic(0, nullptr, 1, false);
  for (int i = 0; i < 20; ++i) mm.mix(out.data(), 4800);
  CHECK(mm.idle(), "music None should end in silence");
}

static bool has(const AudioEvents& ev, Sfx s) {
  for (auto& e : ev)
    if (e.kind == AudioEvent::Kind::Play && e.sfx == s) return true;
  return false;
}
static Music lastMusic(const AudioEvents& ev, Music dflt) {
  for (auto it = ev.rbegin(); it != ev.rend(); ++it)
    if (it->kind == AudioEvent::Kind::SetMusic) return it->music;
  return dflt;
}

static void testSession() {
  WalkSession s(42);
  AudioEvents ev = s.takeAudio();
  CHECK(lastMusic(ev, Music::None) == Music::Dungeon, "a new run starts with the dungeon music");
  AudioEvents all;
  Key keys[] = {Key::Up, Key::Left, Key::Up, Key::Right, Key::Down, Key::Up, Key::Up};
  for (int round = 0; round < 40; ++round)
    for (Key k : keys) {
      s.handle(k);
      if (s.inCombat() || s.atChest() || s.inElevator()) {  // get out of whatever came up
        s.handle(Key::Enter);
        s.handle(Key::Enter);
      }
      for (auto& e : s.takeAudio()) all.push_back(e);
    }
  CHECK(has(all, Sfx::Footstep), "no footsteps");
  CHECK(has(all, Sfx::Footturn), "no turning sounds");
  CHECK(has(all, Sfx::Bonk), "no bonk against a wall");

  // a fight: combat music, hit/miss sounds, and the dungeon music back afterwards
  WalkSession f(7);
  f.takeAudio();
  f.debugFight(false);
  ev = f.takeAudio();
  CHECK(lastMusic(ev, Music::None) == Music::Combat, "fight music");
  bool attacked = false;
  for (int i = 0; i < 400 && f.inCombat() && !f.dead(); ++i) {
    f.handle(Key::None, U'a');
    ev = f.takeAudio();
    attacked |= has(ev, Sfx::AttackHit) || has(ev, Sfx::AttackMiss);
  }
  CHECK(attacked, "no attack sounds in a fight");
  WalkSession b(9);
  b.takeAudio();
  b.debugFight(true);
  CHECK(lastMusic(b.takeAudio(), Music::None) == Music::BossCombat, "boss music");
}

int main() {
  SDL_SetHint("SDL_AUDIO_DRIVER", "dummy");
  testFiles();
  testMap();
  testMixer();
  testSession();
  std::printf(failures ? "audio_test: %d FAILED\n" : "audio_test: ok\n", failures);
  return failures ? 1 : 0;
}
