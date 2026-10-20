#pragma once
#include <array>
#include <string_view>
#include <vector>

namespace laby {

// Everything the game can make you hear, named after the original's SoundManager functions (sound.lua). The game
// (no SDL) only reports these as AudioEvents; src/audio/AudioPlayer turns them into sound using data/audio.json.
enum class Sfx {
  Footstep, Footturn, DoorOpen, TorchFlicker, Bonk, LevelTreasure, RustyLever, ChestOpen, GoldPickup, LevelUp,
  AttackMiss, AttackHit,      // AttackHit: arg = WeaponType
  MonsterAttack, MonsterDies, // arg = MonsterClass
  PlayerHit,                  // arg = PlayerHitSize
  PlayerDied, PlayerHealed, FleeSuccess, FleeFail,
  CharmFailed, CharmSuccess,  // arg = MonsterClass
  Prayer, ScrollPickup, ScrollDiscard, Teleport, SecretSlide, SecretOpens, SecretCloses, FountainDrink, FountainAlarm,
  CrankClick,
  ScrollFailed, ScrollDisintegrated, MagicArrow, HolyWater, MagicMissile, WizardEye, ESP, Sleep, FeebleMind, Invisibility,
  InvisibilityEnds,
  MonsterSpecial,             // arg = MonsterAction: the sting before a monster uses a special action (synthesized, one per action)
  Count
};

// The music tracks (the original's bgm* file players), one plays at a time.
enum class Music { None, Splash, CharCreation, Elevator, Town, Temple, Blacksmith, BeastMart, Combat, BossCombat, Dungeon, TownPortal, SecretDoor, InstantOrb, Count };

enum class PlayerHitSize { Light, Medium, Hard };  // by how much of your max HP the blow took (sound.lua PlayerHit)

inline constexpr std::array<std::string_view, static_cast<size_t>(Sfx::Count)> kSfxNames = {
    "Footstep", "Footturn", "DoorOpen", "TorchFlicker", "Bonk", "LevelTreasure", "RustyLever", "ChestOpen", "GoldPickup", "LevelUp",
    "AttackMiss", "AttackHit", "MonsterAttack", "MonsterDies", "PlayerHit", "PlayerDied", "PlayerHealed", "FleeSuccess", "FleeFail",
    "CharmFailed", "CharmSuccess", "Prayer", "ScrollPickup", "ScrollDiscard", "Teleport", "SecretSlide", "SecretOpens", "SecretCloses",
    "FountainDrink", "FountainAlarm", "CrankClick", "ScrollFailed", "ScrollDisintegrated", "MagicArrow", "HolyWater", "MagicMissile",
    "WizardEye", "ESP", "Sleep", "FeebleMind", "Invisibility", "InvisibilityEnds", "MonsterSpecial"};

inline constexpr std::array<std::string_view, static_cast<size_t>(Music::Count)> kMusicNames = {
    "None", "Splash", "CharCreation", "Elevator", "Town", "Temple", "Blacksmith", "BeastMart", "Combat", "BossCombat", "Dungeon", "TownPortal",
    "SecretDoor", "InstantOrb"};

// One thing for the audio player to do, in the order it happened.
struct AudioEvent {
  enum class Kind { Play, SetMusic, MusicRun, MusicHold, MusicRate };
  Kind kind = Kind::Play;
  Sfx sfx = Sfx::Footstep;
  Music music = Music::None;
  int arg = -1;       // see Sfx; MusicRate: speed in percent; Bonk: how many Hz higher than the base pitch
  int delayMs = 0;    // Play: start this much later
};

using AudioEvents = std::vector<AudioEvent>;

}  // namespace laby
