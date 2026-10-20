// Hand-maintained (it was once generated from the original Lua source).
// Enumerator names match the names used in data/*.json.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace laby {

template <class E> struct EnumEntry { std::string_view name; E value; };
template <class E> struct EnumTable;  // specialized below: static constexpr entries

// Top-level game state machine (Lua: kGame*State).
enum class GameState : std::uint8_t {
  Initial = 0,
  Splash = 1,
  Playing = 2,
  CharacterCreation = 3,
  InTown = 4,
  InCombat = 5,
  Over = 6,
  KilledMonster = 7,
  Treasure = 8,
  CharmedMonster = 9,
  LevelUP = 10,
  EquipmentDetails = 11,
  InElevatorUp = 12,
  InElevatorDown = 13,
  TownPortal = 14,
  TextReader = 15,
  Leaderboard = 16,
  SecretFound = 17,
  SecretClosing = 18,
};
inline constexpr int kGameStateCount = 19;

// Player/monster stats; index into stat arrays.
enum class Stat : std::uint8_t {
  STR = 0,
  DEX = 1,
  INT = 2,
  VIT = 3,
  CHA = 4,
};
inline constexpr int kStatCount = 5;
// The full name of a stat, for messages and panels ("Strength" ... "Charisma").
inline const char* statLongName(std::size_t i) {
  static constexpr const char* kNames[kStatCount] = {"Strength", "Dexterity", "Intelligence", "Vitality", "Charisma"};
  return kNames[i];
}
inline const char* statLongName(Stat s) { return statLongName(static_cast<std::size_t>(s)); }

// Bit flags (Lua: STATUS_*); NORMAL = 0.
enum class StatusFlag : std::uint8_t {
  NORMAL = 0,
  SLEEPING = 1,
  POISONED = 2,
  FURIOUS = 4,
  INVISIBLE = 8,
  UNWAKABLE = 16,
};

// Facing / movement direction (Lua: NORTH/EAST/SOUTH/WEST).
enum class Direction : std::uint8_t {
  NORTH = 0,
  EAST = 1,
  SOUTH = 2,
  WEST = 3,
};
inline constexpr int kDirectionCount = 4;

// What the player did this frame (Lua: MOVED_*).
enum class MoveType : std::uint8_t {
  NONE = 0,
  FORWARD = 1,
  BACKWARD = 2,
};
inline constexpr int kMoveTypeCount = 3;

// Item categories. The first kNumEquipmentTypes are wearable equipment, then Scroll, then potions.
enum class EquipmentType : std::uint8_t {
  Weapon = 0,
  ChestArmor = 1,
  Helmet = 2,
  Boots = 3,
  Gloves = 4,
  Belt = 5,
  Ring = 6,
  Necklace = 7,
  Scroll = 8,
  Health_Potion = 9,
  Mana_Potion = 10,
};
inline constexpr int kEquipmentTypeCount = 11;

enum class WeaponType : std::uint8_t {
  BLUNT = 0,
  SHARP = 1,
  MAGICAL = 2,
};
inline constexpr int kWeaponTypeCount = 3;

enum class MonsterClass : std::uint8_t {
  HUMAN = 0,
  UNDEAD = 1,
  ANIMAL = 2,
  DEMON = 3,
  FAE = 4,
};
inline constexpr int kMonsterClassCount = 5;

enum class ScrollType : std::uint8_t {
  MagicArrow = 0,
  HolyWater = 1,
  Sleep = 2,
  MagicMissile = 3,
  FeebleMind = 4,
  WizardEye = 5,
  ESP = 6,
  Invisibility = 7,
};
inline constexpr int kScrollTypeCount = 8;

// Map tile symbols (Lua: Tile.*); value is the map character.
enum class TileClass : char {
  EMPTY = 32  /* ' ' */,
  WALL = 35  /* '#' */,
  SECRET = 36  /* '$' */,
  SOIL = 37  /* '%' */,
  SECRET_FOUND = 38  /* '&' */,
  O_DOOR = 39  /* '\'' */,
  C_DOOR = 43  /* '+' */,
  FLOOR = 46  /* '.' */,
  UNKNOWN = 60  /* '<' */,
  ELEVATOR = 62  /* '>' */,
  PLAYER = 64  /* '@' */,
  BOSS = 66  /* 'B' */,
  TREASURE = 84  /* 'T' */,
  VEIN = 94  /* '^' */,
  PLAYER_WEST = 97  /* 'a' */,
  PLAYER_EAST = 100  /* 'd' */,
  PLAYER_SOUTH = 115  /* 's' */,
  PLAYER_NORTH = 119  /* 'w' */,
};

// Derived counts (checked against the Lua constants when generated)
inline constexpr int kNumEquipmentTypes = 8;  // wearable equipment: EquipmentType 0..7
inline constexpr int kNumEquipmentPlusScroll = 9;

// MonsterClass display names (Lua: MonsterClassNames)
inline constexpr std::array<std::string_view, 5> kMonsterClassNames = {
    "Human",
    "Undead",
    "Animal",
    "Demon",
    "Fae",
};

template <> struct EnumTable<GameState> {
  static constexpr std::array<EnumEntry<GameState>, 19> entries = {{
      {"Initial", GameState::Initial},
      {"Splash", GameState::Splash},
      {"Playing", GameState::Playing},
      {"CharacterCreation", GameState::CharacterCreation},
      {"InTown", GameState::InTown},
      {"InCombat", GameState::InCombat},
      {"Over", GameState::Over},
      {"KilledMonster", GameState::KilledMonster},
      {"Treasure", GameState::Treasure},
      {"CharmedMonster", GameState::CharmedMonster},
      {"LevelUP", GameState::LevelUP},
      {"EquipmentDetails", GameState::EquipmentDetails},
      {"InElevatorUp", GameState::InElevatorUp},
      {"InElevatorDown", GameState::InElevatorDown},
      {"TownPortal", GameState::TownPortal},
      {"TextReader", GameState::TextReader},
      {"Leaderboard", GameState::Leaderboard},
      {"SecretFound", GameState::SecretFound},
      {"SecretClosing", GameState::SecretClosing},
  }};
};

template <> struct EnumTable<Stat> {
  static constexpr std::array<EnumEntry<Stat>, 5> entries = {{
      {"STR", Stat::STR},
      {"DEX", Stat::DEX},
      {"INT", Stat::INT},
      {"VIT", Stat::VIT},
      {"CHA", Stat::CHA},
  }};
};

template <> struct EnumTable<StatusFlag> {
  static constexpr std::array<EnumEntry<StatusFlag>, 6> entries = {{
      {"NORMAL", StatusFlag::NORMAL},
      {"SLEEPING", StatusFlag::SLEEPING},
      {"POISONED", StatusFlag::POISONED},
      {"FURIOUS", StatusFlag::FURIOUS},
      {"INVISIBLE", StatusFlag::INVISIBLE},
      {"UNWAKABLE", StatusFlag::UNWAKABLE},
  }};
};

template <> struct EnumTable<Direction> {
  static constexpr std::array<EnumEntry<Direction>, 4> entries = {{
      {"NORTH", Direction::NORTH},
      {"EAST", Direction::EAST},
      {"SOUTH", Direction::SOUTH},
      {"WEST", Direction::WEST},
  }};
};

template <> struct EnumTable<MoveType> {
  static constexpr std::array<EnumEntry<MoveType>, 3> entries = {{
      {"NONE", MoveType::NONE},
      {"FORWARD", MoveType::FORWARD},
      {"BACKWARD", MoveType::BACKWARD},
  }};
};

template <> struct EnumTable<EquipmentType> {
  static constexpr std::array<EnumEntry<EquipmentType>, 11> entries = {{
      {"Weapon", EquipmentType::Weapon},
      {"ChestArmor", EquipmentType::ChestArmor},
      {"Helmet", EquipmentType::Helmet},
      {"Boots", EquipmentType::Boots},
      {"Gloves", EquipmentType::Gloves},
      {"Belt", EquipmentType::Belt},
      {"Ring", EquipmentType::Ring},
      {"Necklace", EquipmentType::Necklace},
      {"Scroll", EquipmentType::Scroll},
      {"Health_Potion", EquipmentType::Health_Potion},
      {"Mana_Potion", EquipmentType::Mana_Potion},
  }};
};

template <> struct EnumTable<WeaponType> {
  static constexpr std::array<EnumEntry<WeaponType>, 3> entries = {{
      {"BLUNT", WeaponType::BLUNT},
      {"SHARP", WeaponType::SHARP},
      {"MAGICAL", WeaponType::MAGICAL},
  }};
};

template <> struct EnumTable<MonsterClass> {
  static constexpr std::array<EnumEntry<MonsterClass>, 5> entries = {{
      {"HUMAN", MonsterClass::HUMAN},
      {"UNDEAD", MonsterClass::UNDEAD},
      {"ANIMAL", MonsterClass::ANIMAL},
      {"DEMON", MonsterClass::DEMON},
      {"FAE", MonsterClass::FAE},
  }};
};

template <> struct EnumTable<ScrollType> {
  static constexpr std::array<EnumEntry<ScrollType>, 8> entries = {{
      {"MagicArrow", ScrollType::MagicArrow},
      {"HolyWater", ScrollType::HolyWater},
      {"Sleep", ScrollType::Sleep},
      {"MagicMissile", ScrollType::MagicMissile},
      {"FeebleMind", ScrollType::FeebleMind},
      {"WizardEye", ScrollType::WizardEye},
      {"ESP", ScrollType::ESP},
      {"Invisibility", ScrollType::Invisibility},
  }};
};

template <> struct EnumTable<TileClass> {
  static constexpr std::array<EnumEntry<TileClass>, 18> entries = {{
      {"EMPTY", TileClass::EMPTY},
      {"WALL", TileClass::WALL},
      {"SECRET", TileClass::SECRET},
      {"SOIL", TileClass::SOIL},
      {"SECRET_FOUND", TileClass::SECRET_FOUND},
      {"O_DOOR", TileClass::O_DOOR},
      {"C_DOOR", TileClass::C_DOOR},
      {"FLOOR", TileClass::FLOOR},
      {"UNKNOWN", TileClass::UNKNOWN},
      {"ELEVATOR", TileClass::ELEVATOR},
      {"PLAYER", TileClass::PLAYER},
      {"BOSS", TileClass::BOSS},
      {"TREASURE", TileClass::TREASURE},
      {"VEIN", TileClass::VEIN},
      {"PLAYER_WEST", TileClass::PLAYER_WEST},
      {"PLAYER_EAST", TileClass::PLAYER_EAST},
      {"PLAYER_SOUTH", TileClass::PLAYER_SOUTH},
      {"PLAYER_NORTH", TileClass::PLAYER_NORTH},
  }};
};

// Name <-> enum lookups (exact, case-sensitive names as used in the JSON data).
template <class E>
constexpr std::string_view toString(E v) {
  for (const auto& e : EnumTable<E>::entries)
    if (e.value == v) return e.name;
  return "?";
}

template <class E>
constexpr std::optional<E> fromString(std::string_view s) {
  for (const auto& e : EnumTable<E>::entries)
    if (e.name == s) return e.value;
  return std::nullopt;
}

// Bit-flag operators for StatusFlag.
constexpr StatusFlag operator|(StatusFlag a, StatusFlag b) {
  return static_cast<StatusFlag>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
constexpr StatusFlag operator&(StatusFlag a, StatusFlag b) {
  return static_cast<StatusFlag>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
constexpr StatusFlag operator~(StatusFlag a) {
  return static_cast<StatusFlag>(~static_cast<std::uint8_t>(a));
}
constexpr StatusFlag& operator|=(StatusFlag& a, StatusFlag b) { return a = a | b; }
constexpr StatusFlag& operator&=(StatusFlag& a, StatusFlag b) { return a = a & b; }
constexpr bool hasFlag(StatusFlag v, StatusFlag f) { return (v & f) != StatusFlag::NORMAL; }

}  // namespace laby
