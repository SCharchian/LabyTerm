#include "ui/WalkSession.hpp"

#include <set>
#include <algorithm>
#include <random>
#include <cmath>

#include "core/DataFile.hpp"
#include "core/RandomUtil.hpp"
#include "core/Tuning.hpp"
#include "render/Image8.hpp"
#include "ui/CorridorRenderer.hpp"
#include "game/Scrolls.hpp"
#include "ui/MainScreen.hpp"
#include "ui/PanelKit.hpp"
#include "core/Settings.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/TermLayout.hpp"
#include "ui/TermScreen.hpp"
#include "ui/ShaftArt.hpp"
#include "ui/StatusTag.hpp"

namespace laby {
namespace {

// The map's outer border is never marked seen (it is off the grid), so it shows as wall once a tile beside it is seen.
bool outsideNextToSeen(const Dungeon& d, int x, int y) {
  if (d.inGrid(x, y)) return false;
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i)
      if ((i || j) && d.isTileSeen(x + i, y + j)) return true;
  return false;
}

// The map picture of the tiles x0.., y0.. (cols x rows of them) that the player has seen: the minimap is a window round them, the full
// map the whole floor. The player's arrow is drawn wherever they stand in it.
MapPicture mapPicture(const Dungeon& d, const Walker& w, int x0, int y0, int cols, int rows, bool fountainDry) {
  MapPicture m;
  m.cols = cols;
  m.rows = rows;
  m.facing = static_cast<int>(w.facing);  // Facing order is north, east, south, west
  m.tile.assign(static_cast<size_t>(cols * rows), MapTile::Unseen);
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      const int x = x0 + c, y = y0 + r;
      MapTile t = MapTile::Unseen;
      if (x == w.x && y == w.y) {
        t = MapTile::Player;
      } else if (d.isTileSeen(x, y) || outsideNextToSeen(d, x, y)) {
        switch (d.peekTileType(x, y)) {
          case TileClass::FLOOR: t = MapTile::Floor; break;
          case TileClass::C_DOOR: t = MapTile::DoorClosed; break;
          case TileClass::O_DOOR: t = MapTile::DoorOpen; break;
          case TileClass::SECRET_FOUND: t = fountainDry ? MapTile::FountainDry : MapTile::Fountain; break;  // the opened secret door is the fountain's niche
          case TileClass::ELEVATOR: t = MapTile::Elevator; break;
          case TileClass::BOSS: t = MapTile::Boss; break;
          case TileClass::TREASURE: t = MapTile::Chest; break;
          case TileClass::EMPTY: case TileClass::UNKNOWN: break;
          default: t = MapTile::Wall; break;  // walls, soil, veins, and an unfound secret door
        }
      }
      m.tile[static_cast<size_t>(r * cols + c)] = t;
    }
  return m;
}

// Whatever is in view (in front of you, within sight and with nothing in the way) goes on the map, walls included: the player has seen
// it, however far away and even if the tiles around it were never read by the three-row view.
void markLandmarksInView(Dungeon& d, const Walker& w, double drawDistance, double fovDegrees) {
  const Walker::Basis b = Walker::basisFor(w.facing);
  const int reach = static_cast<int>(std::ceil(drawDistance));
  const double halfFov = fovDegrees * 3.14159265358979 / 360.0 + 0.2;
  const auto open = [&](TileClass t) {
    return t == TileClass::FLOOR || t == TileClass::O_DOOR || t == TileClass::ELEVATOR || t == TileClass::TREASURE || t == TileClass::BOSS;
  };
  for (int a = 0; a <= reach; ++a)
    for (int r = -reach; r <= reach; ++r) {
      if (a == 0 && std::abs(r) != 1) continue;  // only the walls right beside you
      const double dist = std::hypot(a, r);
      if (a > 0 && (dist > drawDistance + 0.5 || std::atan2(std::abs(r), a) > halfFov)) continue;
      const int x = w.x + a * b.ax + r * b.sx, y = w.y + a * b.ay + r * b.sy;
      if (d.isTileSeen(x, y)) continue;
      bool clear = true;
      for (int k = 1; k < static_cast<int>(dist * 4) && clear; ++k) {  // the line of sight, sampled every quarter tile
        const double f = k / (dist * 4);
        // A sample near a tile edge may count as either neighbour, so a corridor's own side walls do not block the view along it.
        const auto cands = [](double v) {
          const double lo = std::floor(v), fr = v - lo;
          return fr > 0.35 && fr < 0.65 ? std::array<int, 2>{static_cast<int>(lo), static_cast<int>(lo) + 1}
                                          : std::array<int, 2>{static_cast<int>(std::lround(v)), static_cast<int>(std::lround(v))};
        };
        bool pass = false;
        for (int ca : cands(a * f))
          for (int cr : cands(r * f)) {
            const int sx = w.x + ca * b.ax + cr * b.sx, sy = w.y + ca * b.ay + cr * b.sy;
            if ((sx == x && sy == y) || (sx == w.x && sy == w.y) || open(d.peekTileType(sx, sy))) pass = true;
          }
        clear = pass;
      }
      if (clear) d.markTileSeen(x, y);
    }
}

std::string signedNumber(int v) { return v > 0 ? "+" + std::to_string(v) : std::to_string(v); }
int toneOf(int v) { return v > 0 ? 3 : v < 0 ? 4 : 2; }

// An item as lines of an overlay column (what the original's item-swap cell and crank view showed).
OverlayColumn itemColumn(const Item& it, const std::string& title, const std::string& footer, bool selected, const Hero* hero = nullptr) {
  OverlayColumn c;
  c.title = title;
  c.footer = footer;
  c.selected = selected;
  if (it.type == EquipmentType::Health_Potion || it.type == EquipmentType::Mana_Potion) {
    c.lines.push_back({it.displayName, "", 1});
    return c;
  }
  if (it.type == EquipmentType::Scroll) {
    c.lines.push_back({it.name, "", 1});
    for (const std::string& d : it.desc) c.lines.push_back({d, "", 0});
    // With a hero, each requirement is green if met and red if not.
    const bool lvlShort = hero && hero->level < it.levelReq;
    const bool intShort = hero && hero->calc[static_cast<size_t>(Stat::INT)] < it.intReq;
    c.lines.push_back({"Needs level " + std::to_string(it.levelReq), "", hero ? (lvlShort ? 4 : 3) : 2});
    c.lines.push_back({"Needs INT " + std::to_string(it.intReq), "", hero ? (intShort ? 4 : 3) : 2});
    c.lines.push_back({"Costs " + std::to_string(it.mpCost) + " MP", "", 2});
    return c;
  }
  c.lines.push_back({it.displayName + " (" + std::to_string(it.level) + ")", "", 1});
  for (size_t i = 0; i < 5; ++i) c.lines.push_back({statLongName(i), signedNumber(it.bonuses[i]), toneOf(it.bonuses[i])});
  c.lines.push_back({"Armor", signedNumber(it.ac), toneOf(it.ac)});
  c.lines.push_back({"HP / MP", signedNumber(it.hp) + "/" + signedNumber(it.mp), toneOf(it.hp + it.mp)});
  return c;
}

// The Inventory sub-menu: the eight slots (empty ones dimmed) and the charmees; the character's numbers under them; the highlighted
// thing's details at the right.
void fillInventoryMenu(MenuPanel& m, const Hero& hero, int sel) {
  static const EquipmentType kOrder[] = {EquipmentType::Weapon, EquipmentType::Helmet, EquipmentType::ChestArmor, EquipmentType::Gloves,
                                         EquipmentType::Boots,  EquipmentType::Belt,   EquipmentType::Ring,       EquipmentType::Necklace};
  const int slots = static_cast<int>(std::size(kOrder)), total = slots + static_cast<int>(hero.charmees.size());
  m.selected = std::clamp(sel, 0, total - 1);
  std::string label;
  for (int i = 0; i < slots; ++i) {
    const auto& it = hero.equipment[static_cast<size_t>(kOrder[i])];
    std::string slot(toString(kOrder[i]));
    slot.resize(13, ' ');
    m.rows.push_back({slot + (it ? it->displayName : "-"), "", 0, it.has_value()});
  }
  for (const Monster& c : hero.charmees) m.rows.push_back({"Charmee      " + c.displayName, "L" + std::to_string(c.level), 2, true});
  const auto stat = [&](size_t i) { return static_cast<int>(std::floor(hero.calc[i])); };
  m.extraTitle = "CHARACTER";
  // The number is green / red where the equipment raises / lowers the stat (or Armor), as the item panels do.
  const auto fact = [](const char* name, int v, int gear) {
    OverlayLine l{name, std::to_string(v), 1};
    if (gear != 0) l.rightTone = toneOf(gear);
    return l;
  };
  std::array<int, 5> gear{};
  int gearAC = 0;
  for (const auto& e : hero.equipment) {
    if (!e) continue;
    for (size_t k = 0; k < 5; ++k) gear[k] += e->bonuses[k];
    gearAC += e->ac;
  }
  for (size_t i = 0; i < 3; ++i) m.extra.push_back(fact(statLongName(i), stat(i), gear[i]));
  m.extraRight.push_back(fact(statLongName(3), stat(3), gear[3]));
  m.extraRight.push_back(fact(statLongName(4), stat(4), gear[4]));
  m.extraRight.push_back(fact("Armor", hero.ac, gearAC));
  m.extraNote = {{"Green", "", 3}, {"/", "", 2}, {"red", "", 4}, {": gear bonus/penalty", "", 2}};
  if (m.selected < slots) {
    const EquipmentType t = kOrder[m.selected];
    const auto& it = hero.equipment[static_cast<size_t>(t)];
    if (it) {
      OverlayColumn col = itemColumn(*it, "", "", true);
      m.detail = col.lines;
      m.detail.insert(m.detail.begin(), {std::string(toString(t)), "", 2});
    } else {
      m.detail.push_back({std::string(toString(t)), "", 2});
      m.detail.push_back({"Empty slot.", "", 2});
    }
  } else {
    const Monster& c = hero.charmees[static_cast<size_t>(m.selected - slots)];
    m.detail.push_back({"Charmee", "", 2});
    m.detail.push_back({c.displayName + " (" + std::to_string(c.level) + ")", "", 1});
    m.detail.push_back({"HP", std::to_string(std::max(0, c.hp)) + "/" + std::to_string(c.maxHP), 0});
    m.detail.push_back({"Armor", std::to_string(c.ac), 0});
    for (size_t k = 0; k < 5; ++k) m.detail.push_back({statLongName(k), std::to_string(static_cast<int>(std::floor(c.stats[k]))), 2});
  }
  m.footer = "[Esc] Back";
}

}  // namespace

WalkSession::WalkSession(std::uint64_t seed)
    : seed_(seed),
      wallView_(WallView::fromDataFile()),
      painter_(wallView_),
      dungeon_(tuning().dungeon.sizeY, tuning().dungeon.sizeX) {
  sys_.setSound([this](Sfx s) { sfx(s); });
  sys_.setCanAbandon([this] { return mode_ == Mode::Playing; });
  sys_.setAbandon([this] { startTitle(); });  // "Abandon run": the character is dropped, nothing is saved
  newDungeon(seed);
}

void WalkSession::say(const std::string& s) {
  log_.push_back(s);
  if (log_.size() > 12) log_.erase(log_.begin());
}

// A floor is a pure function of its seed (a fresh generator state, its own Rng), so it comes back identical every visit; what the
// player did there (memory_) is laid on top.
XY WalkSession::buildFloor(int floor) {
  Rng frng(floorSeed(floor));
  LevelGenState gen;
  const auto looted = [this](int f) { return tracker_.treasureDone(f); };
  XY e;
  if (floor == 1) {  // floor 1 has its own, gentler parameters
    dungeon_.beginNewGame(frng, gen, tuning().dungeon, looted);
    e = XY{dungeon_.level().elevatorX(), dungeon_.level().elevatorY()};
  } else {
    e = dungeon_.beginFloor(floor, frng, gen, tuning().dungeon, looted);
  }
  if (const auto it = memory_.find(floor); it != memory_.end()) dungeon_.recall(it->second);
  return e;
}

void WalkSession::newDungeon(std::uint64_t seed) {
  seed_ = seed;
  rng_.setSeed(seed);
  tracker_ = LevelTracker();
  memory_.clear();
  for (std::uint64_t& s : floorSeeds_) s = rng_.next();
  buildFloor(1);
  // You start on the floor's elevator (the generator already placed it; the original put a second one under the
  // player, which left two on the first floor).
  start_ = XY{dungeon_.level().elevatorX(), dungeon_.level().elevatorY()};
  dungeon_.markTileSeen(start_.x, start_.y);
  dungeon_.revealStartingTiles();
  me_.place(dungeon_, start_.x, start_.y, Direction::NORTH);
  onMap_ = true;
  hero_ = Hero{};
  hero_.roll(rng_);
  fountain_.newRun(rng_);
  secret_.reset();
  pityLeft_ = tuning().encounters.pitySteps;
  alarm_ = false;
  combat_.reset();
  pending_.reset();
  chest_.reset();
  elevator_.reset();
  menu_ = Menu::None;
  portal_ = inTown_ = dead_ = dying_ = false;
  ghost_.reset();
  rebuildFloor_ = false;
  mode_ = Mode::Playing;
  summary_.reset();
  shop_ = restockBlacksmith(rng_, hero_.level);  // the opening stock; after this only a beaten boss restocks
  spot_ = TownSpot::Sign;
  townSel_ = 0;
  sellArmed_ = false;
  log_.clear();
  say("You enter the labyrinth. (seed " + std::to_string(seed) + ")");
  audio_.clear();
  music_ = Music::None;
  syncMusic();
}

void WalkSession::noteLaunchMode(bool terminal) {
  modeHint_.clear();
  if (std::getenv("LABYTERM_NO_TIPS") || stateDir_.empty()) return;
  if (!recordLaunchMode(stateDir_, terminal)) modeHint_ = terminal ? "Play in a graphical window: labyterm -w" : "Play right in your terminal: labyterm -t";
}

void WalkSession::setStateDir(const std::string& dir) {
  stateDir_ = dir;
  hasSave_ = saveExists(dir);
  scores_ = loadScores(dir);
}

enum TitleId { kContinue, kNewRun, kLegendsRow, kOptions, kHelp, kQuit };

std::vector<WalkSession::TitleItem> WalkSession::titleItems() const {
  std::vector<TitleItem> v;
  if (hasSave_) v.push_back({"Continue", kContinue});
  v.push_back({"Enter the Labyrinth", kNewRun});
  v.push_back({"Legends", kLegendsRow});
  v.push_back({"Options", kOptions});
  v.push_back({"Help", kHelp});
  v.push_back({"Quit", kQuit});
  return v;
}

void WalkSession::startTitle() {
  mode_ = Mode::Title;
  hasSave_ = saveExists(stateDir_);
  titleSel_ = 0;
  nameEditing_ = false;
  sys_.close();
  perk_ = false;
  syncMusic();
}

// SplashScreen.lua: A starts. (The original's B read the how-to-play text.)
void WalkSession::titleKey(Key key) {
  const std::vector<TitleItem> items = titleItems();
  const int n = static_cast<int>(items.size());
  if (key == Key::Up || key == Key::Down) {
    sfx(Sfx::CrankClick);
    titleSel_ = (titleSel_ + (key == Key::Up ? n - 1 : 1)) % n;
    return;
  }
  if (key != Key::Enter && key != Key::A) return;
  switch (items[static_cast<size_t>(std::min(titleSel_, n - 1))].id) {
    case kContinue:
      if (!continueGame()) titleSel_ = 0;
      break;
    case kLegendsRow:
      scores_ = loadScores(stateDir_);
      legendsFromTitle_ = true;
      mode_ = Mode::Legends;
      sfx(Sfx::CrankClick);
      break;
    case kNewRun:
      mode_ = Mode::Creation;
      creationSel_ = 2;  // "Enter the Labyrinth!" is under the cursor: the stats are fine, or re-roll
      if (!settings().playerName.empty()) hero_.name = settings().playerName;
      sfx(Sfx::CrankClick);
      syncMusic();
      break;
    case kOptions: sys_.openOptions(); break;
    case kHelp: sys_.openHelp(); break;
    default: sys_.requestQuit(); break;
  }
}

// Player:ShowPlayerCreator: the name, re-rolling the stats and the starting item, and off you go.
void WalkSession::creationKey(Key key) {
  if (key == Key::Up || key == Key::Down) {
    sfx(Sfx::CrankClick);
    creationSel_ = (creationSel_ + (key == Key::Up ? 2 : 1)) % 3;
    nameEditing_ = nameFresh_ = creationSel_ == 0;  // highlighting Name puts the cursor in the field
    return;
  }
  if (key == Key::B) {  // back to the title
    mode_ = Mode::Title;
    sfx(Sfx::CrankClick);
    syncMusic();
    return;
  }
  if (key != Key::Enter && key != Key::A) return;
  switch (creationSel_) {
    case 0: nameEditing_ = nameFresh_ = true; sfx(Sfx::CrankClick); break;  // back into the field (after Esc)
    case 1:  // the stats and the starting item together: no keeping one
      hero_.rollStats(rng_);
      hero_.rollStartingItem(rng_);
      sfx(Sfx::CrankClick);
      break;
    default: beginGame(); break;
  }
}

void WalkSession::beginGame() {
  if (hero_.name.empty()) hero_.name = "D. Crawley";  // the original's default name
  settings().playerName = hero_.name == "D. Crawley" ? "" : hero_.name;  // only a name the player chose is remembered
  settings().save();
  mode_ = Mode::Playing;
  ghost_ = takeGhost(stateDir_);  // the last hero to die is waiting somewhere in the dungeon (once)
  log_.clear();
  say("Welcome to Labyrinth! Death is permanent here, so venture carefully!");
  sfx(Sfx::CrankClick);
  syncMusic();
}

void WalkSession::textInput(char32_t ch) {
  if (!nameEditing_ || ch < 32 || ch >= 127) return;
  if (nameFresh_) hero_.name.clear();
  nameFresh_ = false;
  if (hero_.name.size() < 14) hero_.name.push_back(static_cast<char>(ch));
}

std::shared_ptr<const Image8> WalkSession::monsterImage(const std::string& original) const {
  // "images/Monsters/Human/Ninja" -> <data>/art/monsters/Human/Ninja.png
  const std::string prefix = "images/Monsters/";
  const std::string rel = original.rfind(prefix, 0) == 0 ? original.substr(prefix.size()) : original;
  auto it = images_.find(rel);
  if (it != images_.end()) return it->second;
  Image8 img = Image8::load(dataDir() + "/art/monsters/" + rel + ".png");
  auto ptr = img.empty() ? nullptr : std::make_shared<const Image8>(std::move(img));
  return images_[rel] = ptr;
}

void WalkSession::stopGlide() { lead_ = leadFrom_ = leadTo_ = 0; stepDir_ = 0; }

void WalkSession::startCombat(bool boss) {
  pityLeft_ = tuning().encounters.pitySteps;
  // A dead hero of an earlier run haunts the floor they died on: some encounters are their ghost (and it only comes once).
  const bool ghostFight = !boss && ghost_ && ghost_->floor == dungeon_.floor() && rng_.real() <= tuning().encounters.corpseChance;
  Monster m = ghostFight ? ghost_->monster() : Monster::create(rng_, dungeon_.floor(), dungeon_.floor(), boss, boss && tuning().monsters.boss10EveryFloors > 0 && dungeon_.floor() % tuning().monsters.boss10EveryFloors == 0);
  if (!debugMonster_.empty() && !ghostFight)
    for (const auto& list : MonsterData::get().byClass)
      for (const MonsterDef& d : list)
        if (d.displayName == debugMonster_) { m.displayName = d.displayName; m.image = d.image; m.hover = d.hover; }
  if (!boss && hero_.invisible) {  // Main.lua: only the undead can see you
    if (m.cls != MonsterClass::UNDEAD) {
      say("You slip right past a " + m.displayName);
      return;
    }
    say("A " + m.displayName + " sees you!");
  }
  if (ghostFight) {
    pending_ = ghost_->treasure();  // what it died with, not a new roll
    ghost_.reset();
    say("A " + m.displayName + " appears! It looks kinda familiar...");
  } else {
    pending_ = generateTreasure(rng_, m.level, boss, hero_.hp < hero_.maxHP, hero_.mp < hero_.maxMP, m.isBoss10);
    if (boss || !hero_.invisible) say(boss ? "The boss of this floor, a " + m.displayName + ", attacks!" : "A " + m.displayName + " appears!");
  }
  beginCombat(std::move(m), boss);
}

void WalkSession::beginCombat(Monster m, bool boss) {
  specialSeen_ = 0;
  specialAt_ = 0;
  combat_.emplace(hero_, std::move(m), rng_, [this](const std::string& t) { say(t); }, dungeon_.floor());
  combat_->setCues([this](Sfx s, int arg, int delay) { sfx(s, arg, delay); });
  menuSel_ = 0;
  lockPending_ = animate_ && tuning().encounters.combatInputDelayMs > 0;  // (without a clock, as in the tests, there is no lockout)
  inputLockUntil_ = 0;
  if (boss && animate_) {  // (without a clock, as in the tests, there is no animation)
    bossIntro_ = true;
    introStart_ = 0;
    introFrame_ = 0;
  }
  syncMusic();
}

void WalkSession::endCombat() {
  const CombatOutcome o = combat_->outcome();
  const bool boss = combat_->monster().isBoss;
  if (o == CombatOutcome::Died) {
    if (animate_) {  // the fight stays on screen (the log says what happened) until a key press; without a clock, as in the tests, the run ends at once
      dying_ = true;
      dyingAt_ = now_;
      sfx(Sfx::PlayerDied);
      return;
    }
    endRun();
    return;
  }
  combat_.reset();
  if (boss && (o == CombatOutcome::Won || o == CombatOutcome::Fled || o == CombatOutcome::Charmed)) {
    me_.standingOn = TileClass::FLOOR;  // the boss is gone from the map
    bossBeaten(o == CombatOutcome::Fled);
    say(o == CombatOutcome::Won ? "The floor's boss is dead!" : o == CombatOutcome::Fled ? "The boss fled, but the way is open." : "The floor's boss joins you!");
  }
  if (o == CombatOutcome::Prayed) {
    say("A divine light carries you out of the dungeon!");
    goToTown();
  } else if (o == CombatOutcome::Won) {
    if (pending_) {
      startChest(*pending_, false);
      chest_->bossChest = boss;
    }
  } else if (o == CombatOutcome::Charmed || (boss && o == CombatOutcome::Fled)) {
    afterRewards(boss);  // XP only, no chest (a fled boss takes its loot with it, by design; the portal still appears)
  }
  pending_.reset();
}

// Main.lua's game over: the score gets its step bonus, and the game-over screen takes over from the fight.
void WalkSession::endRun() {
  const Monster& m = combat_->monster();
  RunSummary r;
  r.heroName = hero_.name;
  r.killedBy = m.displayName;
  r.killerLevel = m.level;
  r.killerWasBoss = m.isBoss;
  r.floor = dungeon_.floor();
  r.heroLevel = hero_.level;
  r.steps = hero_.steps;
  r.score = hero_.score;
  r.stepBonus = hero_.steps * tuning().score.perStepTaken;
  r.finalScore = r.score + r.stepBonus;
  hero_.score = r.finalScore;
  writeGhost(stateDir_, Ghost::fromHero(hero_, dungeon_.floor(), rng_));  // you come back for the next hero to fight
  r.rank = recordScore(stateDir_, {hero_.name, r.finalScore, r.floor, r.heroLevel, r.steps});
  scores_ = loadScores(stateDir_);
  summary_ = r;
  combat_.reset();
  pending_.reset();
  chest_.reset();
  menu_ = Menu::None;
  esp_ = perk_ = portal_ = false;
  dead_ = true;
  mode_ = Mode::GameOver;
  if (!dying_) sfx(Sfx::PlayerDied);  // (the death banner already played it)
  dying_ = false;
  syncMusic();
}

// Arcade style: the table only shows when you made it onto it.
void WalkSession::gameOverKey(Key key) {
  if (key != Key::Enter && key != Key::A) return;
  if (summary_ && summary_->rank > 0) {
    legendsFromTitle_ = false;
    mode_ = Mode::Legends;
    sfx(Sfx::CrankClick);
    return;
  }
  newRun(static_cast<std::uint64_t>(ambientRng_()) << 8 ^ ambientRng_());  // the splash screen and a new character
}

void WalkSession::legendsKey(Key key) {
  if (key != Key::Enter && key != Key::A && key != Key::B && key != Key::Escape) return;
  sfx(Sfx::CrankClick);
  if (legendsFromTitle_) {
    mode_ = Mode::Title;
    return;
  }
  newRun(static_cast<std::uint64_t>(ambientRng_()) << 8 ^ ambientRng_());
}

// Town sign post, "Save and Exit": the only way to save. The save is used up when it is loaded.
bool WalkSession::saveAndExit() {
  if (!inTown_ || spot_ != TownSpot::Sign || stateDir_.empty()) return false;
  SaveData d;
  d.seed = seed_;
  d.rng = rng_.state();
  if (!rebuildFloor_) rememberFloor();  // (after a restore dungeon_ is stale: memory_ is already right)
  d.floorSeeds.assign(floorSeeds_.begin(), floorSeeds_.end());
  d.floors = memory_;
  d.hero = hero_;
  d.tracker = tracker_;
  d.shop = shop_;
  d.fountain = fountain_;
  d.ghost = ghost_;
  if (!writeSave(stateDir_, d)) {
    say("Game Save Failed!");
    return false;
  }
  newRun(static_cast<std::uint64_t>(ambientRng_()) << 8 ^ ambientRng_());  // back to the title; the run is in the file now
  return true;
}

bool WalkSession::continueGame() {
  SaveData d;
  const LoadResult r = loadSave(stateDir_, d);
  hasSave_ = saveExists(stateDir_);
  if (r != LoadResult::Ok) return false;
  newDungeon(d.seed);
  if (d.rng.empty() || !rng_.setState(d.rng)) rng_.setSeed(d.seed);
  std::copy(d.floorSeeds.begin(), d.floorSeeds.end(), floorSeeds_.begin());
  memory_ = d.floors;
  hero_ = d.hero;
  tracker_ = d.tracker;
  shop_ = d.shop;
  fountain_ = d.fountain;
  ghost_ = d.ghost;
  rebuildFloor_ = true;
  goToTown();
  log_.clear();
  say("Game restored... Welcome back to Labyrinth!");
  sfx(Sfx::CrankClick);
  syncMusic();
  return true;
}

// Marks the floor's boss dead (or gone) and scores a completed floor.
void WalkSession::bossBeaten(bool) {
  shop_ = restockBlacksmith(rng_, hero_.level);  // LevelTracker.lua: killing a boss restocks the Blacksmith
  if (tracker_.markBoss(dungeon_.floor())) hero_.score += tuning().score.fullyCompletedFloor * dungeon_.floor();
}

// What the original does once rewards are in: the level-up check, and after a boss the town portal appears.
void WalkSession::afterRewards(bool bossReward) {
  if (bossReward) portalAfterPerk_ = true;
  nextPerk();
}

// Player:CheckForLevelUp then DoLevelUp: each level gained is a perk to pick; when none is left, a boss's reward shows the portal.
void WalkSession::nextPerk() {
  if (hero_.gainLevel([this](const std::string& t) { say(t); })) {
    perk_ = true;
    perkSel_ = 0;
    sfx(Sfx::LevelUp);
    return;
  }
  if (portalAfterPerk_) {
    portalAfterPerk_ = false;
    portal_ = true;
    portalSel_ = 0;
    say("A town portal appears!");
  }
}

void WalkSession::perkKey(Key key) {
  if (key == Key::Up || key == Key::Down) {
    sfx(Sfx::CrankClick);
    perkSel_ = (perkSel_ + (key == Key::Up ? 5 : 1)) % 6;
    return;
  }
  if (key != Key::Enter && key != Key::A) return;
  hero_.applyPerk(perkSel_, rng_, [this](const std::string& t) { say(t); });
  sfx(Sfx::CrankClick);
  perk_ = false;
  nextPerk();  // another level may be waiting
}

void WalkSession::buildPerkPanel(MenuPanel& m) const {
  m.place = MenuPanel::Place::Left;
  m.title = "LEVEL UP!";
  m.selected = perkSel_;
  static const char* kRows[] = {"Strength +1", "Dexterity +1", "Intelligence +1", "Vitality +1", "Charisma +1", "<Random> +2"};
  for (const char* r : kRows) m.rows.push_back({r, "", 0, true});
  m.detail.push_back({"You are now level " + std::to_string(hero_.level) + ".", "", 1});
  m.detail.push_back({"Pick a perk:", "", 2});
  if (perkSel_ < 5) {
    const int now = static_cast<int>(std::floor(hero_.calc[static_cast<size_t>(perkSel_)]));
    m.detail.push_back({statLongName(static_cast<size_t>(perkSel_)), std::to_string(now) + " > " + std::to_string(now + 1), 3});
  } else {
    for (const std::string& l : wrap("Labyrinth raises two stats at random.", 25)) m.detail.push_back({l, "", 2});
  }
  m.footer = "[Enter/Z] Choose";
}

void WalkSession::startChest(Treasure tr, bool levelChest) {
  chest_ = Chest{std::move(tr), levelChest};
  chest_->pick = 1;
  say(levelChest ? "Press Enter or Z to open the Level chest!" : "Press Enter to open the chest.");
}

void WalkSession::chestKey(Key key) {
  Chest& c = *chest_;
  const Item& it = c.tr.item;
  if (!c.open) {
    if (key != Key::Enter && key != Key::A) return;
    c.open = true;
    c.openedAt = now_;
    sfx(Sfx::ChestOpen);
    if (it.type == EquipmentType::Health_Potion || it.type == EquipmentType::Mana_Potion) {
      const bool health = it.type == EquipmentType::Health_Potion;
      c.restored = hero_.drink(rng_, health);
      say(std::string(health ? "You've healed " : "You've restored ") + std::to_string(c.restored) + (health ? " HP!" : " MP!"));
    }
    return;
  }
  if (it.isEquipment() && (key == Key::Left || key == Key::Right)) {
    c.pick ^= 1;
    sfx(Sfx::CrankClick);
    return;
  }
  if (key != Key::Enter && key != Key::A) return;
  if (it.isEquipment()) {
    if (c.pick == 1) {
      hero_.equip(it);
      say("You picked up: " + it.displayName);
    } else {
      say("Discarded the " + it.displayName);
    }
  } else if (it.type == EquipmentType::Scroll) {
    if (!hero_.hasScroll(it.scroll)) {
      hero_.addScroll(it);
      sfx(Sfx::ScrollPickup);
      say("You picked up: " + it.name);
    } else {
      sfx(Sfx::ScrollDiscard);
      say("You left the duplicate scroll");
    }
  }
  finishChest();
}

void WalkSession::finishChest() {
  const int gold = static_cast<int>(std::floor(chest_->tr.gold));
  hero_.gold += gold;
  sfx(Sfx::GoldPickup);
  say("You found " + std::to_string(gold) + " Gold Pieces!");
  const bool levelChest = chest_->levelChest, bossChest = chest_->bossChest;
  if (levelChest) {
    me_.standingOn = TileClass::FLOOR;  // the chest is gone from the map
    if (tracker_.markTreasure(dungeon_.floor())) hero_.score += tuning().score.fullyCompletedFloor * dungeon_.floor();
  }
  chest_.reset();
  afterRewards(bossChest);
}

int WalkSession::menuCount() const {
  if (menu_ == Menu::Scrolls) return static_cast<int>(hero_.scrolls.size());
  return static_cast<int>(hero_.equipment.size() + hero_.charmees.size());  // every slot is listed, empty or not
}

void WalkSession::openMenu(Menu m) {
  menu_ = m;
  subSel_ = 0;
  sfx(Sfx::CrankClick);
}

std::string WalkSession::scrollBlocked(const Item& sc) const {
  if (combat_ ? !sc.combat : !sc.utility) return combat_ ? "Not in a fight" : "Combat only";
  if (hero_.level < sc.levelReq) return "Needs level " + std::to_string(sc.levelReq);
  if (hero_.calc[static_cast<size_t>(Stat::INT)] < sc.intReq) return "Needs INT " + std::to_string(sc.intReq);
  if (hero_.mp < sc.mpCost) return "Not enough MP";
  return "";
}

void WalkSession::menuKey(Key key, char32_t ch) {
  const Menu was = menu_;
  const int n = menuCount();
  if (key == Key::Escape || key == Key::B || ch == U'x' || (was == Menu::Scrolls && ch == U's') || (was == Menu::Inventory && (ch == U'i' || ch == U'c'))) {
    menu_ = Menu::None;
    sfx(Sfx::CrankClick);
    return;
  }
  if (!combat_ && ch == U's' && was == Menu::Inventory) { openMenu(Menu::Scrolls); return; }
  if (!combat_ && (ch == U'i' || ch == U'c') && was == Menu::Scrolls) { openMenu(Menu::Inventory); return; }
  if (n > 0 && (key == Key::Up || key == Key::Down)) {
    sfx(Sfx::CrankClick);
    subSel_ = (subSel_ + (key == Key::Up ? n - 1 : 1)) % n;
    return;
  }
  if (was == Menu::Scrolls && (key == Key::Enter || key == Key::A) && subSel_ < n) {
    const Item& sc = hero_.scrolls[static_cast<size_t>(subSel_)];
    if (const std::string why = scrollBlocked(sc); !why.empty()) {
      sfx(Sfx::ScrollFailed);
      say(sc.name + ": " + why + ".");
      if (combat_) menu_ = Menu::None;  // back to the fight's action menu; the reason is in the log
      return;
    }
    menu_ = Menu::None;
    castScroll(sc);
  }
}

// Scrolls.lua ReadScroll: in a fight the Combat class does it all; outside one only Wizard Eye and Invisibility can be cast.
void WalkSession::castScroll(const Item& scroll) {
  if (combat_) {
    const ScrollType type = scroll.scroll;
    combat_->cast(type);
    noteSpecial();
    if (combat_->takeEsp()) esp_ = true;
    if (combat_->outcome() != CombatOutcome::Ongoing) endCombat();
    return;
  }
  const Item sc = scroll;
  switch (payForScroll(hero_, rng_, sc, dungeon_.floor())) {
    case CastResult::NoMana:
      sfx(Sfx::Bonk);
      say("Insufficient MP!");
      return;
    case CastResult::Disintegrated:
      sfx(Sfx::ScrollDisintegrated);
      say(sc.name + " Scroll DISINTEGRATED!");
      return;
    case CastResult::Cast: break;
  }
  if (sc.scroll == ScrollType::WizardEye) {
    sfx(Sfx::WizardEye);
    say("You cast Wizard Eye! The map is revealed!");
    for (int y = 1; y <= tuning().dungeon.sizeY; ++y)  // (tiles are numbered from 1)
      for (int x = 1; x <= tuning().dungeon.sizeX; ++x) dungeon_.markTileSeen(x, y);
    dungeon_.level().secretRevealed = true;
  } else if (sc.scroll == ScrollType::Invisibility) {
    sfx(Sfx::Invisibility);
    say("You cast Invisibility! You are now invisible to most monsters!");
    hero_.invisible = true;
    hero_.invisibleSince = hero_.steps;
  }
}

// Scrolls.lua UpdateStatusEffects: invisibility lasts at least a few steps, then may wear off, more likely with each one.
void WalkSession::invisibilityTick() {
  if (!hero_.invisible) return;
  const auto& t = tuning().scrolls;
  const int age = hero_.steps - hero_.invisibleSince;
  if (age <= t.invisibilityMinTurns) return;
  if (rng_.real() <= age * t.invisibilityExpiryPerTurn) {
    hero_.invisible = false;
    sfx(Sfx::InvisibilityEnds);
    say("Your invisibility has worn off!");
  }
}

void WalkSession::debugGiveKit() {
  for (const Item& sc : ItemData::get().scrolls) hero_.addScroll(sc);
  for (int t = 0; t < 8; ++t) hero_.equip(randomItem(rng_, static_cast<EquipmentType>(t), 5));
  hero_.charmees.clear();
  for (int i = 0; i < 2; ++i) hero_.charmees.push_back(Monster::create(rng_, 3, 3, false));
  hero_.gold = 500;
  hero_.level = std::max(hero_.level, 4);
  hero_.recalc();
  hero_.mp = hero_.maxMP / 2;
}

// The elevator ride: the dungeon's own cart, seen from one cell away, over the shaft scrolling past it (ShaftArt). The cart is traced
// once; each frame shifts it by the shake and paints the shaft behind it. Going up the shaft slides down the picture, going down it slides up.
ViewLayers WalkSession::elevatorPicture(int pw, int ph, int density, std::uint64_t nowMs) const {
  const auto& v = tuning().view;
  std::uint64_t key = 1469598103934665603ull;
  const auto mixV = [&](auto x) {
    for (size_t i = 0; i < sizeof x; ++i) key = (key ^ reinterpret_cast<const unsigned char*>(&x)[i]) * 1099511628211ull;
  };
  for (double x : {v.fovDegrees, v.eyeHeight, v.drawDistance, v.fog}) mixV(x);
  for (int i : {v.supersample, pw, ph, density}) mixV(i);
  if (key != cartKey_ || cartCache_.objects.px.empty()) {
    const TileLookup lookup = [](int a, int r) {
      TileInfo t;
      t.mx = r;
      t.my = -a;
      t.tile = (r < -2 || r > 2 || a > 7) ? TileClass::WALL : TileClass::FLOOR;
      if (a == 1 && r == 0) t.tile = TileClass::ELEVATOR;
      if (a == 0 && r == 0) t.tile = TileClass::PLAYER;
      return t;
    };
    CorridorCamera cam;
    cam.fovDegrees = v.fovDegrees;
    cam.eyeHeight = v.eyeHeight;
    cam.drawDistance = v.drawDistance;
    cam.fog = v.fog;
    cam.supersample = std::max(1, v.supersample / density);
    cam.lineScale = density;
    cartCache_ = toLayers(renderCorridor(pw, ph, lookup, cam));
    cartCache_.density = density;
    cartCache_.background = cartCache_.walls = Bitmap1(pw, ph);  // the dungeon's own floor, ceiling and walls are not part of the ride
    cartKey_ = key;
  }
  // the cage rattles while it moves, and settles over the 0.3 s before the run-on ends
  const double moving = elevator_ && nowMs < elevatorAnimUntil_ ? std::clamp((static_cast<double>(elevatorAnimUntil_) - static_cast<double>(nowMs)) / 300.0, 0.0, 1.0) : 0.0;
  const CartShake sh = cartShake(elevatorShakeMs_);
  const int dx = static_cast<int>(std::lround(sh.dx * moving * density / 2.0)), dy = static_cast<int>(std::lround(sh.dy * moving * density / 2.0));
  const auto shifted = [&](const Bitmap1& b) {
    Bitmap1 out(b.w, b.h);
    if (b.w) out.blit(b, dx, dy);
    return out;
  };
  ViewLayers cart = cartCache_;  // the cart as it is this frame: everything of it moves together
  cart.objects = shifted(cart.objects);
  cart.shade = shifted(cart.shade);
  for (auto& t : cart.tints) {
    if (t.lit.w) t.lit = shifted(t.lit);
    if (t.shade.w) t.shade = shifted(t.shade);
  }
  ViewLayers L = cart;
  L.shade = L.background = L.walls = Bitmap1(pw, ph);
  paintShaft(L, elevatorScroll_);
  shadeBehindCart(L, cart);  // the timbers behind the cart are in its shadow, not cutting through it
  L.shade.blit(cart.shade, 0, 0);  // the cart's own shaded parts go over the shaft's
  return L;
}

void WalkSession::startElevator() {
  elevator_ = Elevator{dungeon_.floor()};
  elevatorAnimUntil_ = 0;
  say("You step into the elevator.");
  sfx(Sfx::RustyLever);
}

void WalkSession::elevatorTick(bool up) {
  Elevator& e = *elevator_;
  if (up) {
    e.level = std::max(e.level - 1, 0);  // one above floor 1 is the town: Enter there gets off (leaveElevator)
  } else {
    e.level = std::min(e.level + 1, tuning().dungeon.maxFloor);
  }
}

// The crank: a click, and the music plays while the shaft slides past and you keep at it (the original paused it 0.4 s after the last turn;
// here a ride is one key press and the picture moves for 2 s, so the music runs that long).
void WalkSession::elevatorCranked() {
  if (now_ >= elevatorAnimUntil_) elevatorAnimLast_ = now_;  // (the animation was idle: restart its clock)
  elevatorAnimUntil_ = now_ + 2000;
  sfx(Sfx::CrankClick);
  elevatorRunUntil_ = elevatorAnimUntil_;
  audio_.push_back({AudioEvent::Kind::MusicRate, Sfx::Footstep, Music::None, 100, 0});
  if (!elevatorMusicRuns_) audio_.push_back({AudioEvent::Kind::MusicRun, Sfx::Footstep, Music::None, -1, 0});
  elevatorMusicRuns_ = true;
}

void WalkSession::elevatorKey(Key key) {
  if (key == Key::Up || key == Key::Down) elevatorCranked();
  switch (key) {
    case Key::Up: elevatorUp_ = true; elevatorTick(true); break;
    case Key::Down: elevatorUp_ = false; elevatorTick(false); break;
    case Key::Enter:
    case Key::A: leaveElevator(); break;
    default: break;
  }
}

void WalkSession::leaveElevator() {
  const int level = elevator_->level;
  if (level == 0) {  // the way to town
    sfx(Sfx::RustyLever);
    goToTown();
    return;
  }
  if (tracker_.bossDone(level)) {  // a cleared floor cannot be entered again
    sfx(Sfx::Bonk);
    say("You've already cleared level " + std::to_string(level) + ". There's nothing left to do here.");
    return;
  }
  elevator_.reset();
  sfx(Sfx::RustyLever);
  say("You've entered level " + std::to_string(level) + "!");
  if (level != dungeon_.floor() || rebuildFloor_) newFloor(level);
  else placeAtElevator();
}

void WalkSession::newFloor(int floor) {
  mapOpen_ = false;
  if (!rebuildFloor_) rememberFloor();
  rebuildFloor_ = false;
  const XY e = buildFloor(floor);
  start_ = e;
  alarm_ = false;  // a fountain's alarm lasts to the next floor
  me_.place(dungeon_, e.x, e.y, me_.facing);
  dungeon_.revealStartingTiles();
  onMap_ = true;
}

// Back at the elevator of the floor that is already built (the original rebuilds only when the floor changes).
void WalkSession::placeAtElevator() {
  if (onMap_) me_.lift(dungeon_);
  me_.place(dungeon_, dungeon_.level().elevatorX(), dungeon_.level().elevatorY(), me_.facing);
  dungeon_.revealStartingTiles();
  onMap_ = true;
}

void WalkSession::goToTown() {
  mapOpen_ = false;
  if (onMap_) me_.lift(dungeon_);
  onMap_ = false;
  elevator_.reset();
  inTown_ = true;
  spot_ = TownSpot::Sign;
  townSel_ = 0;
  sellArmed_ = false;
  shopDetail_ = false;
  say("Welcome to town, " + hero_.name + "!");
}

// Town.lua TownUpdate, with the sign post as a list: Temple, Blacksmith, BeastMart, and the elevator down to the Labyrinth.
// (The original has no inn: the temple heals, for free.)
void WalkSession::townKey(Key key, char32_t ch) {
#ifdef LABYTERM_DEBUG_KEYS
  if (ch == U'r') {  // DEBUG: a free rest anywhere
    hero_.hp = hero_.maxHP;
    hero_.mp = hero_.maxMP;
    sfx(Sfx::PlayerHealed);
    say("(debug) You rest and recover.");
    return;
  }
#endif
  const bool ok = key == Key::Enter || key == Key::A, back = key == Key::B || key == Key::Escape || ch == U'x';
  const bool up = key == Key::Up, down = key == Key::Down;
  const auto move = [&](int n) {
    if (n <= 0 || !(up || down)) return;
    sfx(Sfx::CrankClick);
    townSel_ = (townSel_ + (up ? n - 1 : 1)) % n;
    sellArmed_ = false;
  };
  if (spot_ == TownSpot::Sign) {
    move(5);
    if (!ok) return;
    switch (townSel_) {
      case 0:
        spot_ = TownSpot::Temple;
        say("The Temple welcomes you, " + hero_.name + ". Stay a while, and listen.");
        break;
      case 1:
        spot_ = TownSpot::Blacksmith;
        say("You follow the sounds of ringing steel.");
        break;
      case 2:
        spot_ = TownSpot::BeastMart;
        say("Be smart, shop at BeastMart!");
        break;
      case 3:
        inTown_ = false;
        elevator_ = Elevator{0};
        elevatorAnimUntil_ = 0;
        say("You steel yourself and shuffle back to the elevator...");
        return;
      default:
        sfx(Sfx::CrankClick);
        saveAndExit();
        return;
    }
    sfx(Sfx::CrankClick);
    townSel_ = 0;
    sellArmed_ = false;
    shopDetail_ = false;
    return;
  }
  if (back && sellArmed_) {  // BeastMart: "no" to the offer
    sellArmed_ = false;
    sfx(Sfx::CrankClick);
    return;
  }
  if (back && shopDetail_) {
    shopDetail_ = false;
    sfx(Sfx::CrankClick);
    return;
  }
  if (back) {  // out of a shop: the sign is highlighted on the shop's door
    townSel_ = spot_ == TownSpot::Temple ? 0 : spot_ == TownSpot::Blacksmith ? 1 : 2;
    spot_ = TownSpot::Sign;
    sellArmed_ = false;
    sfx(Sfx::CrankClick);
    return;
  }
  switch (spot_) {
    case TownSpot::Temple:
      if (!ok) return;
      hero_.score += (hero_.maxHP - hero_.hp) * tuning().score.perHPHealed;
      hero_.hp = hero_.maxHP;
      hero_.mp = hero_.maxMP;
      sfx(Sfx::PlayerHealed);
      say("All HP/MP healed to full!");
      return;
    case TownSpot::Blacksmith:
      if (shopDetail_) {  // the item's page: Up/Down show the next / previous item, Enter buys (and returns to the list)
        move(static_cast<int>(shop_.size()));
        if (ok && buyItem()) shopDetail_ = false;
        return;
      }
      move(static_cast<int>(shop_.size()));
      if (ok && !shop_.empty()) {
        shopDetail_ = true;
        sfx(Sfx::CrankClick);
      }
      return;
    case TownSpot::BeastMart:
      if (sellArmed_) {  // the Yes / No buttons
        if (key == Key::Left || key == Key::Right) {
          sellNo_ = !sellNo_;
          sfx(Sfx::CrankClick);
        } else if (ok && sellNo_) {
          sellArmed_ = false;
          sfx(Sfx::CrankClick);
        } else if (ok) {
          sellCharmee();
        } else {
          move(2);
        }
        return;
      }
      move(2);
      if (ok) sellCharmee();
      return;
    default: return;
  }
}

// Blacksmith.lua DoEquipmentSales
bool WalkSession::buyItem() {
  if (townSel_ >= static_cast<int>(shop_.size())) return false;
  const Item item = shop_[static_cast<size_t>(townSel_)];
  const int price = itemPrice(item);
  const bool scroll = item.type == EquipmentType::Scroll;
  if (scroll && hero_.hasScroll(item.scroll)) {
    sfx(Sfx::Bonk);
    say("You already have one; it's very nice...");
    return false;
  }
  if (hero_.gold < price) {
    sfx(Sfx::Bonk);
    say("You need more gold!");
    return false;
  }
  hero_.gold -= price;
  sfx(Sfx::GoldPickup);
  if (scroll) {
    hero_.addScroll(item);
    sfx(Sfx::ScrollPickup);
    say(item.name + " bought for " + std::to_string(price) + " gold.");
  } else {
    const auto& worn = hero_.equipment[static_cast<size_t>(item.type)];
    say(worn ? worn->displayName + " replaced with " + item.displayName : item.displayName + " bought for " + std::to_string(price) + " gold.");
    hero_.equip(item);
  }
  shop_.erase(shop_.begin() + townSel_);  // the slot is gone until the next boss restocks
  townSel_ = std::clamp(townSel_, 0, std::max(0, static_cast<int>(shop_.size()) - 1));
  return true;
}

// BeastMart.lua: sells the highlighted charmee (a second Enter confirms; the original sold at once).
void WalkSession::sellCharmee() {
  if (townSel_ >= static_cast<int>(hero_.charmees.size())) {
    sfx(Sfx::Bonk);
    return;
  }
  if (!sellArmed_) {
    sellArmed_ = true;
    sellNo_ = false;
    sfx(Sfx::CrankClick);
    return;
  }
  const Monster m = hero_.charmees[static_cast<size_t>(townSel_)];
  const int price = beastPrice(m);
  hero_.gold += price;
  hero_.score += m.level * tuning().score.multiplierSoldCharmee;
  hero_.charmees.erase(hero_.charmees.begin() + townSel_);
  sellArmed_ = false;
  sfx(Sfx::GoldPickup);
  say(m.displayName + " SOLD for " + std::to_string(price) + " gold!");
}

void WalkSession::portalKey(Key key, char32_t ch) {
  if (key == Key::Left || key == Key::Right) {
    sfx(Sfx::CrankClick);
    portalSel_ ^= 1;
  } else if ((key == Key::Enter || key == Key::A) && portalSel_ == 0) {
    portal_ = false;
    sfx(Sfx::Teleport);
    goToTown();
  } else if (key == Key::Enter || key == Key::A || key == Key::B || key == Key::Escape || ch == U'b') {
    portal_ = false;
    say("You stay in the dungeon.");
  }
}

void WalkSession::useAction(CombatAction a) {
  if (a == CombatAction::Scroll) {  // choosing one is a sub-menu; casting it is what takes the turn
    if (std::string why; !combat_->available(a, &why)) say(why);
    else openMenu(Menu::Scrolls);
    return;
  }
  combat_->act(a);
  noteSpecial();
  if (combat_->outcome() != CombatOutcome::Ongoing) endCombat();
}

// A monster special action was just used: show its banner when the sting plays (the monster answers 0.5 s after you).
void WalkSession::noteSpecial() {
  if (!combat_) return;
  const auto& s = combat_->lastSpecial();
  if (s.seq == specialSeen_) return;
  specialSeen_ = s.seq;
  if (!animate_) return;  // (without a clock, as in the tests, there is no animation)
  specialAt_ = now_ + 500;
  specialKind_ = static_cast<int>(s.action);
}

// The death banner is up: Enter/Z/Esc moves on to the game-over screen, after a short guard so a hammered key doesn't skip it unread.
void WalkSession::dyingKey(Key key) {
  if (repeat_ || now_ < dyingAt_ + 400) return;
  if (key == Key::Enter || key == Key::A || key == Key::Escape) endRun();
}

void WalkSession::combatKey(Key key, char32_t ch) {
  if (dead_) return;
  if (repeat_ || lockPending_ || now_ < inputLockUntil_) return;  // a held walking key must not move the menu once the fight starts (Up would land on Trick)
  switch (key) {
    case Key::Left:
    case Key::Right: menuSel_ ^= 1; return;
    case Key::Up: menuSel_ = (menuSel_ + 4) % 6; return;
    case Key::Down: menuSel_ = (menuSel_ + 2) % 6; return;
    case Key::Enter:
    case Key::A: useAction(static_cast<CombatAction>(menuSel_)); return;
    default: break;
  }
#ifdef LABYTERM_DEBUG_KEYS
  if (ch == U'w') {  // DEBUG: win the fight instantly (XP, loot and chest as usual)
    combat_->debugKill();
    say("(debug) Instant win.");
    endCombat();
    return;
  }
#endif
  static const char kHot[] = "acshtp";  // (A is the audio key outside fights; R also reads a scroll)
  if (ch == U'r') ch = U's';
  for (int i = 0; i < kCombatActionCount; ++i)
    if (ch == static_cast<char32_t>(kHot[i])) {
      menuSel_ = i;
      useAction(static_cast<CombatAction>(i));
      return;
    }
}

void WalkSession::debugExplore(int steps) {
  std::vector<std::pair<XY, int>> queue{{{me_.x, me_.y}, 0}};
  std::set<std::pair<int, int>> done{{me_.x, me_.y}};
  for (size_t i = 0; i < queue.size(); ++i) {
    const auto [p, n] = queue[i];
    for (int j = -1; j <= 1; ++j)
      for (int k = -1; k <= 1; ++k) dungeon_.markTileSeen(p.x + k, p.y + j);
    if (n >= steps) continue;
    for (const XY d : {XY{1, 0}, XY{-1, 0}, XY{0, 1}, XY{0, -1}}) {
      const TileClass t = dungeon_.peekTileType(p.x + d.x, p.y + d.y);
      if ((t == TileClass::FLOOR || t == TileClass::O_DOOR || t == TileClass::C_DOOR) && done.insert({p.x + d.x, p.y + d.y}).second) queue.push_back({{p.x + d.x, p.y + d.y}, n + 1});
    }
  }
}

int WalkSession::debugCountTiles(TileClass t) const {
  int n = 0;
  for (int y = 0; y < tuning().dungeon.sizeY; ++y)
    for (int x = 0; x < tuning().dungeon.sizeX; ++x) {
      TileClass c = dungeon_.peekTileType(x, y);
      if (onMap_ && x == me_.x && y == me_.y) c = me_.standingOn;
      n += c == t;
    }
  return n;
}

void WalkSession::debugRideElevator() {
  if (onMap_ && me_.standingOn == TileClass::ELEVATOR && !chest_ && !combat_ && !elevator_) startElevator();
}

bool WalkSession::debugGoNextTo(TileClass t) {
  if (!onMap_) return false;
  const int w = tuning().dungeon.sizeX, h = tuning().dungeon.sizeY;
  static const int kDx[] = {0, 1, 0, -1}, kDy[] = {-1, 0, 1, 0};  // N E S W
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      if (dungeon_.peekTileType(x, y) != t) continue;
      for (int d = 0; d < 4; ++d) {
        const int nx = x - kDx[d], ny = y - kDy[d];  // stand behind the tile, looking at it
        if (dungeon_.peekTileType(nx, ny) == TileClass::FLOOR) {
          me_.lift(dungeon_);
          me_.place(dungeon_, nx, ny, static_cast<Direction>(d));
          return true;
        }
      }
    }
  return false;
}

bool WalkSession::handle(Key key, char32_t ch, bool repeat) {
  repeat_ = repeat;
  hpHurt_ = false;
  const bool handled = handleKey(key, ch);
  syncMusic();
  return handled;
}

bool WalkSession::handleKey(Key key, char32_t ch) {
  if (bossIntro_) return true;  // the boss's entrance: nothing to do until it is over
  if (sys_.open()) {
    sys_.key(key, ch);
    return true;
  }
  if (nameEditing_) {  // typing the name: Enter or Esc is done, Up/Down leave the field, Backspace deletes (the characters come from textInput)
    if (key == Key::Backspace && !hero_.name.empty()) {
      hero_.name.pop_back();
      nameFresh_ = false;
    } else if (key == Key::Up || key == Key::Down) {
      if (hero_.name.empty()) hero_.name = "D. Crawley";
      creationKey(key);
    }
    else if (key == Key::Enter || key == Key::Escape) {
      nameEditing_ = false;
      if (hero_.name.empty()) hero_.name = "D. Crawley";  // nothing typed: the default
      if (key == Key::Enter) creationSel_ = 1;            // on to Re-Roll Character
      sfx(Sfx::CrankClick);
    }
    return true;
  }
  if (dying_) {
    dyingKey(key);
    return true;
  }
  const bool afterDeath = mode_ == Mode::GameOver || (mode_ == Mode::Legends && !legendsFromTitle_);
  if (key == Key::Escape && afterDeath) {  // after a death Esc goes straight back to the start screen
    sfx(Sfx::CrankClick);
    newRun(static_cast<std::uint64_t>(ambientRng_()) << 8 ^ ambientRng_());
    return true;
  }
  if (key == Key::Escape && menu_ == Menu::None && !esp_ && !escBacksOut()) {  // (in a sub-menu, Esc goes back; the Esc menu opens from the game)
    sys_.openMain();
    return true;
  }
  if (mode_ == Mode::Legends) {
    legendsKey(key);
    return true;
  }
  if (mode_ == Mode::GameOver) {
    gameOverKey(key);
    return true;
  }
  if (mode_ == Mode::Title) {
    titleKey(key);
    return true;
  }
  if (mode_ == Mode::Creation) {
    creationKey(key);
    return true;
  }
  if (perk_) {
    perkKey(key);
    return true;
  }
  if (secret_) {
    secretKey(key, ch);
    return true;
  }
  if (chest_) {
    chestKey(key);
    return true;
  }
  if (portal_) {
    portalKey(key, ch);
    return true;
  }
  if (inTown_) {
    townKey(key, ch);
    return true;
  }
  if (elevator_) {
    elevatorKey(key);
    return true;
  }
  if (esp_) {
    esp_ = false;
    return true;
  }
  if (mapOpen_) {  // the full map: M, Esc, Enter or Z/X close it
    if (key == Key::Escape || key == Key::Enter || key == Key::A || key == Key::B || ch == U'm' || ch == U'x') mapOpen_ = false;
    return true;
  }
  if (menu_ != Menu::None) {
    menuKey(key, ch);
    return true;
  }
  if (combat_) {
    combatKey(key, ch);
    return true;
  }
#ifdef LABYTERM_DEBUG_KEYS
  if (ch == U'h' && !dead_) {  // DEBUG: a free rest (the temple is in town)
    hero_.hp = hero_.maxHP;
    hero_.mp = hero_.maxMP;
    sfx(Sfx::PlayerHealed);
    say("(debug) You rest and recover.");
    return true;
  }
  if ((ch == U'o' || ch == U'p') && !dead_) {  // DEBUG: a chest with a health (O) or mana (P) orb, to watch the animation
    debugPotionChest(ch == U'o');
    return true;
  }
  if (ch == U'l' && !dead_) {  // DEBUG: level up now, with the XP that level takes (so the XP stays in step with the level)
    hero_.experience = std::max(hero_.experience, Hero::expNeeded(hero_.level));
    nextPerk();
    return true;
  }
  if (ch == U'g') {  // DEBUG: toggle random monster encounters
    noSpawns_ = !noSpawns_;
    say(std::string("(debug) Monster spawning ") + (noSpawns_ ? "OFF" : "ON") + ".");
    return true;
  }
#endif
  if (ch == U'm') {
    mapOpen_ = true;
    sfx(Sfx::CrankClick);
    return true;
  }
  if (ch == U'i' || ch == U'c') {
    openMenu(Menu::Inventory);
    return true;
  }
  if (ch == U's') {
    if (hero_.scrolls.empty()) say("You don't have any Scrolls!");
    else openMenu(Menu::Scrolls);
    return true;
  }
  StepResult r;
  switch (key) {
    case Key::Up: r = me_.stepForward(dungeon_); break;
    case Key::Down: r = me_.stepBackward(dungeon_); break;
    case Key::Left: me_.turnLeft(); stopGlide(); sfx(Sfx::Footturn); return true;
    case Key::Right: me_.turnRight(); stopGlide(); sfx(Sfx::Footturn); return true;
    default: return false;
  }
  if (r == StepResult::Bumped) {
    say("Bonk!");
    sfx(Sfx::Bonk, bonkPitch());
  } else if (r == StepResult::FoundSecret) {
    say("You found a secret door!");
    sfx(Sfx::LevelTreasure);
    openSecret();
  } else if (r == StepResult::SecretOpenAlready) {
    say("The secret door is already open.");
    sfx(Sfx::Bonk, bonkPitch());
  } else if (me_.doorOpened) {
    say("You open the door.");
  }
  if (r == StepResult::Moved) {
    ++hero_.steps;
    hero_.stepRegen(tuning().dungeon.regenSteps);
    invisibilityTick();
    sfx(Sfx::Footstep);
    if (me_.doorOpened) sfx(Sfx::DoorOpen, -1, 60);
    if (animate_) {  // the eye is left a cell behind (or ahead) and glides in
      stepDir_ = key == Key::Up ? 1 : -1;
      lead_ = leadFrom_ = leadTo_ = std::clamp(lead_ + stepDir_, -1.0, 1.0);
    }
    if (me_.standingOn == TileClass::ELEVATOR) {
      startElevator();  // (the lever sound is made there)
      return true;
    }
    // Main.lua: a random encounter (never at the level treasure), or the boss if you stand on his tile
    const bool atBoss = me_.standingOn == TileClass::BOSS && !tracker_.bossDone(dungeon_.floor());
    bool roll = me_.standingOn != TileClass::TREASURE && rng_.range(1, 100) < encounterPercent();  // (always rolled, so the sequence is the same either way)
#ifdef LABYTERM_DEBUG_KEYS
    if (noSpawns_) roll = false;  // DEBUG: no random encounters (the floor's boss still waits on his tile)
#endif
    if (pityLeft_ > 0) {  // pity timer: no random encounter yet (the boss still triggers)
      --pityLeft_;
      roll = false;
    }
    if (roll || atBoss) {
      startCombat(atBoss);
    } else if (me_.standingOn == TileClass::TREASURE) {
      sfx(Sfx::LevelTreasure);
      const int f = dungeon_.floor();  // Score_TreasureFound: less for floors above your level
      hero_.score += hero_.level < f ? static_cast<int>(std::floor(tuning().score.perLevelTreasureFound * f * (static_cast<double>(hero_.level) / f)))
                                     : tuning().score.perLevelTreasureFound * f;
      startChest(generateTreasure(rng_, dungeon_.floor(), true, false, false), true);  // the floor's level chest
    }
  }
  return true;
}

bool WalkSession::wallAhead() const {
  const Walker::Basis b = Walker::basisFor(me_.facing);
  switch (dungeon_.peekTileType(me_.x + b.ax, me_.y + b.ay)) {
    case TileClass::FLOOR: case TileClass::O_DOOR: case TileClass::ELEVATOR: case TileClass::BOSS: case TileClass::TREASURE: return false;
    default: return true;
  }
}

// Where the eye wants to be: with the player, or, while a fight or its chest holds up a forward step, half a cell (0.7 facing a wall) short of it.
double WalkSession::leadTarget() const {
  if (elevator_ || inTown_ || stepDir_ <= 0) return 0;
  return combat_ || chest_ ? (wallAhead() ? 0.7 : 0.5) : 0;  // a bit further back facing a wall, to show more floor
}

// The Bonk gets higher the closer you are to the floor's secret door (while it is still shut), as in the original.
int WalkSession::bonkPitch() const {
  const Level& l = dungeon_.level();
  if (!l.hasSecretDoor() || l.secretOpen) return 0;
  const double dist = std::hypot(me_.x - l.secretX(), me_.y - l.secretY());
  return std::max(-100, static_cast<int>(35 - 94 * dist / 35));  // 25% wider than the original's 75, floored so far-off bonks stay audible
}

Music WalkSession::wantedMusic() const {
  if (mode_ == Mode::Title || mode_ == Mode::Legends) return Music::Splash;
  if (mode_ == Mode::Creation) return Music::CharCreation;
  if (dead_) return Music::Splash;  // the original's game over plays the title screen's rain
  if (chest_ && chest_->open && (chest_->tr.item.type == EquipmentType::Health_Potion || chest_->tr.item.type == EquipmentType::Mana_Potion)) return Music::InstantOrb;
  if (secret_ && secret_->phase == SecretDoor::Phase::Opening) return Music::SecretDoor;  // its grinding plays while the door slides
  if (portal_) return Music::TownPortal;
  if (inTown_) {
    switch (spot_) {
      case TownSpot::Temple: return Music::Temple;
      case TownSpot::Blacksmith: return Music::Blacksmith;
      case TownSpot::BeastMart: return Music::BeastMart;
      default: return Music::Town;
    }
  }
  if (elevator_) return Music::Elevator;
  if (combat_) return combat_->monster().isBoss ? Music::BossCombat : Music::Combat;
  return Music::Dungeon;
}

void WalkSession::syncMusic() {
  const Music want = wantedMusic();
  if (want == music_) return;
  music_ = want;
  audio_.push_back({AudioEvent::Kind::SetMusic, Sfx::Footstep, want, -1, 0});
  if (want == Music::Elevator) {  // plays while you crank, then pauses (see elevatorCranked)
    elevatorRunUntil_ = now_ + 400;
    elevatorMusicRuns_ = true;
  } else {
    elevatorMusicRuns_ = false;
  }
}

bool WalkSession::tick(std::uint64_t nowMs) {
  now_ = nowMs;
  if (hero_.hp < lastHp_) {  // damage: the HP bar shows it until the next key press
    if (!hpHurt_) hurtFrom_ = lastHp_;
    hpHurt_ = true;
  }
  lastHp_ = hero_.hp;
  if (lockPending_) {
    lockPending_ = false;
    inputLockUntil_ = nowMs + static_cast<std::uint64_t>(tuning().encounters.combatInputDelayMs);
  }
  syncMusic();
  if (mode_ != Mode::Playing) return true;  // the storm moves
  bool pending = false;
  if (elevatorMusicRuns_) {  // when the shaft has stopped sliding the music pauses
    if (nowMs >= elevatorRunUntil_) {
      audio_.push_back({AudioEvent::Kind::MusicHold, Sfx::Footstep, Music::None, -1, 0});
      elevatorMusicRuns_ = false;
    } else {
      pending = true;
    }
  }
  if (elevator_ && nowMs < elevatorAnimUntil_) {  // the shaft slides past (down the picture going up, up it going down) and the cage rattles
    const double dt = static_cast<double>(nowMs - elevatorAnimLast_);
    elevatorAnimLast_ = nowMs;
    elevatorScroll_ += (elevatorUp_ ? 1 : -1) * 0.125 * dt;  // units per ms: about 125 a second, a full picture in a second
    elevatorShakeMs_ += dt;
    pending = true;
  }
  if (elevator_ && elevator_->level == 0 && elevatorAnimUntil_ > 0 && nowMs >= elevatorAnimUntil_) {
    leaveElevator();  // cranked all the way up and the shaft has stopped moving: off to town (with the lever sound)
    pending = true;
  }
  if (inTown_ && !elevator_ && spot_ == TownSpot::Temple) pending = true;  // the candle flames
  if (portal_) pending = true;  // the portal animation
  if (chest_ && chest_->open && !chest_->tr.item.isEquipment()) pending = true;  // the potion orb and the scroll picture
  torch_.setActive(focused_ && torchWanted());
  bool crackle = false;
  if (torch_.update(nowMs, ambientRng_, crackle)) torchDirty_ = true;
  if (crackle) sfx(Sfx::TorchFlicker);  // the original: a random flicker sound with each flicker of the dungeon picture
  animate_ = true;
  const bool intro = tickIntro(nowMs);
  const bool secret = tickSecret(nowMs);
  const bool glide = tickGlide(nowMs);
  if (combat_ && specialAt_ != 0 && nowMs < specialAt_ + kBannerMs) pending = true;
  if (dying_) pending = true;  // the prompt pulses
  if ((combat_ && combat_->monster().isBoss10) || lairGlow_) pending = true;  // the Boss10's glow breathes
  return glide || pending || intro || secret;
}

// The boss-encounter animation (LevelBoss_Encounter, 27 frames at 100 ms) plays in place of the view when the fight starts.
bool WalkSession::tickIntro(std::uint64_t nowMs) {
  if (!bossIntro_) return false;
  if (introStart_ == 0) introStart_ = nowMs + 1;
  const int frame = static_cast<int>((nowMs + 1 - introStart_) / 100);
  if (frame >= 27) {
    bossIntro_ = false;
    return true;
  }
  introFrame_ = frame;
  return true;  // keep the loop on its short timer for the whole animation, not just on the tick a frame changes
}

// The secret door (SecretDoor.lua): the original had you turn the crank to slide it open; here it opens by itself as soon as you
// step into it, and the fountain behind it is then offered. Leaving without a drink closes it again (it can be found once more).
void WalkSession::openSecret() {
  dungeon_.level().secretRevealed = true;
  secret_ = SecretDoor{};
  if (!animate_) {  // (without a clock, as in the tests, there is no animation)
    finishSecretOpening();
    return;
  }
  sfx(Sfx::SecretOpens, -1, tuning().dungeon.secretDoorMs);  // the thud as it settles
}

bool WalkSession::tickSecret(std::uint64_t nowMs) {
  if (!secret_ || secret_->phase == SecretDoor::Phase::Offer) return false;
  SecretDoor& d = *secret_;
  if (d.start == 0) d.start = nowMs + 1;
  const bool opening = d.phase == SecretDoor::Phase::Opening;
  const double dur = std::max(1, tuning().dungeon.secretDoorMs) * (opening ? 1.0 : 0.5);
  const double u = std::clamp(static_cast<double>(nowMs + 1 - d.start) / dur, 0.0, 1.0);
  d.slide = opening ? u : 1 - u;
  if (u >= 1.0) {
    if (opening) {
      finishSecretOpening();
    } else {
      Level& l = dungeon_.level();
      l.tile(l.secretY(), l.secretX()).cls = TileClass::SECRET;  // shut again (ResetSecretDoor)
      secret_.reset();
    }
  }
  return true;  // keep the loop on its short timer for the whole slide
}

void WalkSession::finishSecretOpening() {
  Level& l = dungeon_.level();
  l.secretOpen = true;
  if (tracker_.markSecretPaid(dungeon_.floor())) hero_.score += tuning().score.perLevelSecretFound;  // once per floor for the whole run (the original paid at every opening)
  if (tracker_.fountainDone(dungeon_.floor())) {
    say("The fountain is drained.");
    secret_.reset();
    return;
  }
  fountain_.setColor(static_cast<int>(floorSeed(dungeon_.floor()) >> 33));  // the floor's fountain always has the same colour
  secret_ = SecretDoor{SecretDoor::Phase::Offer, 0, 1.0};
  fountainSel_ = 0;
  say("A hidden fountain waits behind the door!");
}

void WalkSession::secretKey(Key key, char32_t ch) {
  if (secret_->phase != SecretDoor::Phase::Offer) return;  // it moves by itself: nothing to do until it has finished
  if (key == Key::Left || key == Key::Right) fountainSel_ ^= 1;
  else if (key == Key::Enter || key == Key::A) {
    if (fountainSel_ == 0) drinkFountain();
    else closeSecret();
  } else if (key == Key::B || key == Key::Escape || ch == U'x') closeSecret();
}

void WalkSession::closeSecret() {
  Level& l = dungeon_.level();
  l.secretOpen = false;
  l.secretRevealed = true;
  secret_ = SecretDoor{SecretDoor::Phase::Closing, 0, 1.0};
  if (!animate_) {
    l.tile(l.secretY(), l.secretX()).cls = TileClass::SECRET;
    secret_.reset();
    return;
  }
  sfx(Sfx::SecretCloses, -1, std::max(1, tuning().dungeon.secretDoorMs) / 2);
}

// Lands the hero in room `roomId`: in its centre, or on the floor next to it when the centre is taken (the elevator can sit there,
// and standing on it would give no ride, as events only fire on a step).
void WalkSession::teleportToRoom(int roomId) {
  const Level& l = dungeon_.level();
  const Pos centre = l.room(roomId).center;
  const Pos at = l.tile(centre.r, centre.c).cls == TileClass::FLOOR ? centre : l.floorNear(centre);
  me_.lift(dungeon_);
  me_.place(dungeon_, at.c, at.r, me_.facing);
}

// Fountain:TakeDrink: the colour's effect, then the fountain is drained for good (the door stays open).
void WalkSession::drinkFountain() {
  const FountainEffect e = fountain_.effect();
  const auto talk = [this](const std::string& t) { say(t); };
  secret_.reset();
  if (e != FountainEffect::ShadowFight) sfx(Sfx::FountainDrink);  // (the shadow comes without a drink)
  switch (e) {
    case FountainEffect::Heal:
      Fountain::heal(hero_, talk);
      break;
    case FountainEffect::StatUp:
      Fountain::statChange(hero_, rng_, +1, talk);
      break;
    case FountainEffect::StatDown:
      Fountain::statChange(hero_, rng_, -1, talk);
      break;
    case FountainEffect::Teleport: {
      teleportToRoom(static_cast<int>(rng_.range(1, static_cast<std::int64_t>(dungeon_.level().rooms().size()))));
      stopGlide();
      sfx(Sfx::Teleport);
      say("You feel a strange sensation...");
      say("   ...You are somewhere else!");
      break;
    }
    case FountainEffect::ShadowFight:
      startShadowFight();
      break;
    case FountainEffect::Invisibility:
      sfx(Sfx::Invisibility);
      say("You fade from view, becoming invisible!");
      hero_.invisible = true;
      hero_.invisibleSince = hero_.steps;
      break;
    case FountainEffect::Alarm:
      sfx(Sfx::FountainAlarm);
      say("You hear an ominous horn!");
      alarm_ = true;
      break;
  }
  fountain_.markDrunk();
  hero_.score += tuning().score.perLevelFountainDrink;
  tracker_.markFountain(dungeon_.floor());
  say("The fountain is drained.");
}

// Fountain:DoShadowFight: a dark copy of you, with your current health, stats and armour.
void WalkSession::startShadowFight() {
  pityLeft_ = tuning().encounters.pitySteps;  // like any fight (startCombat does this for the others): a few free steps after it
  say("You peer into the fountain and see");
  say("a dark reflection of yourself...");
  say("       ...It attacks!");
  Monster m;
  m.displayName = "Dark " + hero_.name;
  m.cls = MonsterClass::HUMAN;
  m.image = "images/Monsters/PlayerCorpse";
  m.level = hero_.level;
  m.ac = hero_.ac;
  m.maxHP = hero_.maxHP;
  m.hp = hero_.hp;
  m.experience = 1 + static_cast<int>(std::ceil(hero_.experience * 0.10));
  m.stats = hero_.base;
  const int lvl = static_cast<int>(std::floor(statLevelNormalize(rng_, m.level, std::floor(1 + m.level / 8.0))));
  pending_ = generateTreasure(rng_, lvl, false, hero_.hp < hero_.maxHP, hero_.mp < hero_.maxMP);
  beginCombat(std::move(m), false);
}

int WalkSession::encounterPercent() const { return alarm_ ? tuning().encounters.alarmChancePercent : tuning().encounters.monsterChancePercent; }

bool WalkSession::tickGlide(std::uint64_t nowMs) {
  if (elevator_ || inTown_) { lead_ = leadFrom_ = leadTo_ = 0; stepDir_ = 0; return false; }
  const double target = leadTarget();
  if (target != leadTo_) {  // a new leg of the glide, from wherever the eye is now
    leadFrom_ = lead_;
    leadTo_ = target;
    leadStart_ = nowMs;
    leadDur_ = std::max(60.0, 170.0 * std::abs(leadTo_ - leadFrom_));
  }
  const double prev = lead_;
  const double u = std::clamp(static_cast<double>(nowMs - leadStart_) / leadDur_, 0.0, 1.0);
  lead_ = u >= 1.0 ? leadTo_ : leadFrom_ + (leadTo_ - leadFrom_) * (1 - (1 - u) * (1 - u));  // eases out: slows into the stop
  if (u >= 1.0 && leadTo_ == 0) stepDir_ = 0;
  return u < 1.0 || lead_ != prev;
}

// The town's screens as panels: the sign post, the temple, the Blacksmith's stock and BeastMart's two offers.
// What the keys do where you are in town: shown in the map's place (the town has no map). The first line is a heading.
void WalkSession::buildTownKeys(std::vector<OverlayLine>& g) const {
  const auto keys = [&](const char* k, const char* what) { g.push_back({k, what, 0}); };
  switch (spot_) {
    case TownSpot::Sign:
      g.push_back({"Pick a place", "", 1});
      keys("Up/Down", "Choose");
      keys("Enter", "Go");
      keys("Esc", "Menu");
      return;
    case TownSpot::Temple:
      g.push_back({"Heal all HP/MP", "", 1});
      keys("Enter", "Heal");
      keys("Esc", "Back");
      return;
    case TownSpot::Blacksmith:
      g.push_back({shopDetail_ ? "Buy this?" : "Blacksmith", "", 1});
      keys("Up/Down", shopDetail_ ? "Next item" : "Choose");
      keys("Enter", shopDetail_ ? "Buy" : "Details");
      keys("Esc", "Back");
      return;
    case TownSpot::BeastMart:
      g.push_back({sellArmed_ ? "Sell it?" : "BeastMart", "", 1});
      if (!sellArmed_) keys("Up/Down", "Choose");
      if (sellArmed_) keys("Left/Right", "Choose");
      keys("Enter", sellArmed_ ? "Confirm" : "Sell");
      keys("Esc", sellArmed_ ? "No" : "Back");
      return;
  }
}

void WalkSession::buildTownGuide(std::vector<OverlayLine>& g) const {
  buildTownKeys(g);
  while (g.size() < 4) g.push_back({"", "", 0});
  g.push_back({"Gold", std::to_string(hero_.gold) + "g", 5});  // always the bottom line of the panel
}

// The words on the town's picture (see TownArt.hpp): the square's caption, and the shops' boxes drawn inside the picture.
// The terminal's view is smaller than the SDL window's and its text is one row tall (the SDL's is two), so there the lines sit half as far
// apart, in a box that is as wide as the view leaves it.
void WalkSession::buildTownLabels(std::vector<SceneLabel>& out, int vpCols, int vpRows, bool term) const {
  using A = SceneLabel::Align;
  const TownScene scene = spot_ == TownSpot::Temple ? TownScene::Temple : spot_ == TownSpot::Blacksmith ? TownScene::Blacksmith : spot_ == TownSpot::BeastMart ? TownScene::BeastMart : TownScene::Square;
  const int ox = townMarginCols(scene, vpCols);  // (a wide view has the scene in its middle)
  const ShopGeom geom = shopGeom(std::min(vpCols, townDesignCols(scene)), vpRows);
  const auto Y = [&](int sdlRow) { return term ? (sdlRow + 1) / 2 : sdlRow; };
  const int footY = term ? vpRows - 2 : vpRows - 3;  // the line above the box's bottom edge
  const auto gold = [&](int v) { return std::to_string(v) + "g"; };
  const auto add = [&](int cy, const std::string& text, const std::string& right, int tone, int rightTone, bool filled, A align, int cx = -1000, int w = -1) {
    SceneLabel l;
    l.align = align;
    l.filled = filled;
    l.cx = (cx == -1000 ? geom.innerX : cx) + ox;
    l.cy = cy;
    l.w = w < 0 ? geom.innerW : w;
    l.h = term ? 1 : 2;
    l.tone = tone;
    l.rightTone = rightTone;
    l.text = text;
    l.right = right;
    out.push_back(l);
  };
  const int captionY = term ? vpRows - 1 : vpRows - 2;
  switch (spot_) {
    case TownSpot::Sign: {
      static const char* kAbout[] = {"Heal all HP and MP.", "Buy equipment and scrolls.", "Sell your Charmees.", "Back down the elevator.", "Save and exit. A save works once."};
      add(captionY, kAbout[std::clamp(townSel_, 0, 4)], "", 0, 0, false, A::Center, 1 - ox, vpCols - 2);
      return;
    }
    case TownSpot::Temple:
      add(captionY, "Press Enter to heal all HP and MP", "", 1, 0, false, A::Center, 1 - ox, vpCols - 2);
      return;
    case TownSpot::Blacksmith: {
      if (!shopDetail_) {  // the list: "For Sale", then each item and its price
        add(Y(1), "For Sale", "", 1, 0, false, A::Left);
        for (size_t i = 0; i < shop_.size(); ++i) {
          const Item& it = shop_[i];
          const bool owned = it.type == EquipmentType::Scroll && hero_.hasScroll(it.scroll);
          const int price = itemPrice(it);
          add(Y(4 + 2 * static_cast<int>(i)), it.type == EquipmentType::Scroll ? it.name : it.displayName, owned ? "owned" : gold(price), owned ? 2 : it.masterwork ? 5 : 0,
              owned ? 2 : hero_.gold >= price ? 5 : 4, static_cast<int>(i) == townSel_, A::Left);
        }
        add(footY, "[Enter] Details", "", 2, 0, false, A::Center);
        return;
      }
      if (townSel_ >= static_cast<int>(shop_.size())) return;
      const Item& it = shop_[static_cast<size_t>(townSel_)];
      std::vector<OverlayLine> detail = itemColumn(it, "", "", true, &hero_).lines;
      if (it.isEquipment()) {  // each number beside what you wear now
        const auto& worn = hero_.equipment[static_cast<size_t>(it.type)];
        const Item cur = worn ? *worn : emptyItem(it.type);
        static const char* kShort[] = {"STR", "DEX", "INT", "VIT", "CHA"};
        detail.resize(1);
        detail[0].left += worn ? "" : " (new)";
        const auto row = [&](const std::string& label, int now, int old) { {  // columns line up with the "this (worn)" header: the number ends under the "s" of "this"
          std::string a = signedNumber(now), b = "(" + signedNumber(old) + ")";
          a.insert(0, a.size() < 4 ? 4 - a.size() : 0, ' ');
          b.resize(std::max<size_t>(b.size(), 6), ' ');
          detail.push_back({label, a + " " + b, now > old ? 3 : now < old ? 4 : 2});
        } };
        for (size_t i = 0; i < 5; ++i) row(kShort[i], it.bonuses[i], cur.bonuses[i]);
        row("AC", it.ac, cur.ac);
        row("HP", it.hp, cur.hp);
        row("MP", it.mp, cur.mp);
        detail.push_back({"Wearing: " + (worn ? worn->displayName : std::string("nothing")), "", 2});
        detail.insert(detail.begin() + 1, {"", "this (worn)", 2});  // what the two numbers are
      }
      for (size_t i = 0; i < detail.size() && i < 12; ++i) {
        const bool head = i == 0;
        add(Y(1 + 2 * static_cast<int>(i) + (head ? 0 : 1)), detail[i].left, detail[i].right, head ? 1 : detail[i].tone == 0 ? 0 : detail[i].tone, detail[i].tone, false, head ? A::Center : A::Left);
      }
      const bool owned = it.type == EquipmentType::Scroll && hero_.hasScroll(it.scroll);
      add(footY, owned ? "You own this scroll." : "[Enter] Buy " + gold(itemPrice(it)), "", owned ? 2 : hero_.gold >= itemPrice(it) ? 0 : 4, 0, false, A::Center);
      return;
    }
    case TownSpot::BeastMart: {
      for (int k = 0; k < 2; ++k) {
        const int y0 = 1 + k * geom.slotRows;
        const bool on = k == townSel_, has = k < static_cast<int>(hero_.charmees.size());
        // a slot's lines, by their row in the SDL window's 15-row slot: the name, the offer, the price (or the buttons), the hint
        const auto line = [&](int dy, const std::string& text, int tone, A align = A::Center) {
          const bool big = !term && !text.empty();  // big letters: the small ones washed out on the bright slot (the solid fill rows stay full width)
          const int row = term ? (dy == 0 ? 0 : dy == 1 ? 1 : dy == 4 ? 3 : dy == 7 ? 5 : dy == 10 ? 7 : dy / 2) : dy;
          add(y0 + row, text, "", tone, 0, on, align, big ? geom.innerX + 1 : -1000, big ? geom.innerW - 2 : -1);
          if (big) out.back().scale = 1.4f;
        };
        if (on)  // the chosen slot is solid, gaps included
          for (int dy = 0; dy < geom.slotRows - 1; dy += term ? 1 : 2) add(y0 + dy, "", "", 0, 0, true, A::Center);
        if (!has) {
          line(1, "Empty Slot", 1, A::Left);
          line(7, "<No Charmee>", 2);
          continue;
        }
        const Monster& c = hero_.charmees[static_cast<size_t>(k)];
        line(1, c.displayName + " L" + std::to_string(c.level), 1, A::Left);
        const std::string price = std::to_string(beastPrice(c)) + " Gold";
        if (on && sellArmed_) {
          line(4, "Sell for " + price + "?", 5);
        } else {
          line(4, "BeastMart offers:", 0, A::Left);
          line(7, price, 5);
        }
        if (on && sellArmed_) {  // "Sell?" and two buttons side by side
          const int bw = term ? (geom.innerW - 2) / 2 : 21;
          for (int b = 0; b < 2; ++b) {
            add(y0 + (term ? 5 : 7), b == 0 ? "Yes" : "No", "", 0, 0, sellNo_ == (b == 1), A::Center, geom.innerX + (term ? 0 : 2) + b * (bw + (term ? 2 : 1)), bw);
            out.back().button = true;
          }
        } else if (on) {
          line(10, "[Enter] Accept", 1);
        }
      }
      return;
    }
  }
}

void WalkSession::draw(CellGrid& grid, const Theme& theme, const std::string& statusTag) const {
  if (mode_ != Mode::Playing) {
    if (mode_ == Mode::Title) {
      std::vector<std::string> rows;
      int big = 0;
      for (const TitleItem& t : titleItems()) {
        if (t.id == kNewRun) big = static_cast<int>(rows.size());
        rows.push_back(t.label);
      }
      if (terminalUi()) drawTitleScreenTerm(grid, theme, now_, rows, titleSel_, big, modeHint_);
      else drawTitleScreen(grid, theme, now_, rows, titleSel_, big, modeHint_);
    } else if (mode_ == Mode::Legends) {
      LegendsInfo info;
      for (const ScoreEntry& e : scores_) info.rows.push_back({e.name, e.score, e.floor});
      info.highlight = !legendsFromTitle_ && summary_ ? summary_->rank : 0;
      info.fromGameOver = !legendsFromTitle_;
      if (terminalUi()) drawLegendsScreenTerm(grid, theme, now_, info);
      else drawLegendsScreen(grid, theme, now_, info);
    } else if (mode_ == Mode::GameOver) {
      GameOverInfo info;
      if (summary_) {
        const RunSummary& r = *summary_;
        info.name = r.heroName;
        info.facts = {{"Slain by", "a " + r.killedBy, 4},
                      {"Its level", std::to_string(r.killerLevel) + (r.killerWasBoss ? " (the floor's boss)" : ""), 4},
                      {"Floor reached", std::to_string(r.floor), 0},
                      {"Hero level", std::to_string(r.heroLevel), 0},
                      {"Steps taken", std::to_string(r.steps), 0}};
        info.score = {{"Score", std::to_string(r.score), 0},
                      {"Step bonus (" + std::to_string(r.steps) + " steps)", "+" + std::to_string(r.stepBonus), 3},
                      {"Final score", std::to_string(r.finalScore), 5}};
        if (r.rank > 0) info.score.push_back({"A new Legend!", "Rank " + std::to_string(r.rank), 3});
      }
      if (terminalUi()) drawGameOverScreenTerm(grid, theme, now_, info);
      else drawGameOverScreen(grid, theme, now_, info);
    } else {
      CreationInfo info;
      info.name = hero_.name;
      info.editing = nameEditing_;
      info.selected = creationSel_;
      for (size_t i = 0; i < 5; ++i) info.statLines.push_back({statLongName(i), std::to_string(static_cast<int>(std::floor(hero_.calc[i]))), 0});
      info.statLines.push_back({"", "", 0});
      info.statLines.push_back({"HP", std::to_string(hero_.maxHP), 3});
      info.statLines.push_back({"MP", std::to_string(hero_.maxMP), 0});
      info.statLines.push_back({"Armor", std::to_string(hero_.ac), 0});
      info.itemTitle = "Starting item";
      if (hero_.startingItem) info.itemLines = itemColumn(*hero_.startingItem, "", "", true).lines;
      if (terminalUi()) drawCreationScreenTerm(grid, theme, now_, info);
      else drawCreationScreen(grid, theme, now_, info);
    }
    if (sys_.open()) {
      MenuPanel panel;
      sys_.build(panel);
      if (terminalUi() && panel.place == MenuPanel::Place::Left) panel.place = MenuPanel::Place::Dialog;
      if (terminalUi()) {  // the panel over the middle of the screen
        const TermLayout tl = termLayout(grid.width(), grid.height());
        drawMenuOverTerm(grid, theme, panel, tl.vx + (tl.vpCols - std::min(tl.vpCols, 60)) / 2, 3, std::min(tl.vpCols, 60), tl.vpRows - 3);
      } else {
        drawMenuOver(grid, theme, panel, (kGridCols - kViewportCols) / 2, 6, kViewportCols, kViewportRows);
      }
    }
    return;
  }
  viewTiles(dungeon_, me_);  // marks the tiles around you as seen, as the original does
  if (!inTown_ && !elevator_) markLandmarksInView(dungeon_, me_, tuning().view.drawDistance, tuning().view.fovDegrees);
  // The Text maze style traces the dungeon at twice the sub-pixel density (4x8 per cell: enough to tell '|' from '/' from '_');
  // the lair's entrance is the original's picture and stays a mosaic; the town's pictures follow the style; the elevator ride is drawn like the dungeon.
  const int density = settings().mazeStyle == 1 && !bossIntro_ ? 2 : 1;
  // The terminal build lays the screen out for its own size (TermLayout.hpp); the SDL window has the fixed layout.
  const bool term = terminalUi();
  const TermLayout tl = termLayout(grid.width(), grid.height());
  const int vpCols = term ? tl.vpCols : kViewportCols, vpRows = term ? tl.vpRows : kViewportRows;
  const int pw = vpCols * 2 * density, ph = vpRows * 4 * density;
  ViewLayers layers;
  bool lairInView = false;
  TownPicture townPic;  // (the labels must outlive drawMainScreen)
  if (!elevator_) {
    // The view: rays traced through the map, at the picture's own size
    const Walker::Basis b = Walker::basisFor(me_.facing);
    const TileLookup lookup = [&](int a, int r) {
      TileInfo t;
      t.mx = me_.x + a * b.ax + r * b.sx;
      t.my = me_.y + a * b.ay + r * b.sy;
      t.tile = (a == 0 && r == 0) ? TileClass::PLAYER : dungeon_.peekTileType(t.mx, t.my);
      return t;
    };
    CorridorCamera cam;
    const auto& v = tuning().view;
    cam.fovDegrees = v.fovDegrees;
    cam.eyeHeight = v.eyeHeight;
    cam.drawDistance = v.drawDistance;
    cam.fog = v.fog;
    // The Text style's picture has density x density times the pixels, so it takes fewer samples per pixel (view.supersample / density,
    // at least 1): the same number of rays as the mosaic's, which is what keeps a step or turn cheap. lineScale = density keeps its
    // lines the weight they have in the mosaic (measured against the full-supersample trace: closest of 1 to 3; more breaks walls into gaps).
    cam.supersample = std::max(1, v.supersample / density);
    cam.lineScale = density;
    cam.backOff = lead_;
    cam.heldAhead = std::min(0.95 - lead_, wallAhead() ? 0.33 : 9.0);  // the chest stays about 0.95 from the eye, but never inside a wall
    if (secret_) cam.secretSlide = secret_->slide;
    cam.secretTell = focused_ && torchWanted() && torch_.flickering();
    drawnTell_ = cam.secretTell;  // the original: a flicker reveals the secret door
    cam.fountainDry = tracker_.fountainDone(dungeon_.floor());
    if (chest_ && !chest_->open) cam.heldObject = chest_->levelChest ? 3 : 1;  // the chest in front of you
    cam.frame = {me_.x, me_.y, b.ax, b.ay, b.sx, b.sy};
    cam.lair = &painter_.lair();
    // everything the trace depends on: the camera, and the tiles within sight
    std::uint64_t key = 1469598103934665603ull;
    const auto mix = [&](const void* d, size_t n) {
      for (size_t i = 0; i < n; ++i) key = (key ^ static_cast<const unsigned char*>(d)[i]) * 1099511628211ull;
    };
    const auto mixV = [&](auto v) { mix(&v, sizeof v); };
    for (double d : {cam.fovDegrees, cam.eyeHeight, cam.drawDistance, cam.fog, cam.heldAhead, cam.backOff, cam.secretSlide}) mixV(d);
    for (int i : {cam.heldObject, cam.supersample, pw, ph, cam.frame.px, cam.frame.py, cam.frame.ax, cam.frame.ay}) mixV(i);
    mixV(cam.secretTell);
    mixV(cam.fountainDry);
    mixV(reinterpret_cast<std::uintptr_t>(cam.lair));
    const int reach = static_cast<int>(std::ceil(cam.drawDistance)) + 1;
    bool lairSeen = false;
    for (int a = -1; a <= reach; ++a)
      for (int r = -reach; r <= reach; ++r) {
        const TileClass tc = lookup(a, r).tile;
        lairSeen = lairSeen || tc == TileClass::BOSS;
        mixV(static_cast<int>(tc));
      }
    lairInView = lairSeen;
    if (key != viewKey_ || viewCache_.walls.px.empty()) {
      viewCache_ = toLayers(renderCorridor(pw, ph, lookup, cam));
      viewCache_.density = density;
      viewKey_ = key;
    }
    layers = viewCache_;
  }
  if (elevator_) layers = elevatorPicture(pw, ph, density, now_);
  const int lw = layers.walls.w / layers.density, lh = layers.walls.h / layers.density;  // the original's 241 x 118, or the picture's size
  if (bossIntro_) {  // the animation frame on the original's white canvas (white = lit, black = dark)
    const Bitmap1& frame = painter_.image("Monsters/LevelBoss_Encounter-table-" + std::to_string(std::clamp(introFrame_, 0, 26) + 1));
    layers.background = layers.walls = layers.objects = layers.shade = Bitmap1(lw, lh);
    std::fill(layers.walls.px.begin(), layers.walls.px.end(), Bitmap1::Light);
    layers.blitOriginal(layers.objects, frame, 0, 0);
  }
  if (elevator_) {
    // (the ride picture is made above)
  } else if (inTown_) {
    const TownScene scene = spot_ == TownSpot::Temple ? TownScene::Temple : spot_ == TownSpot::Blacksmith ? TownScene::Blacksmith : spot_ == TownSpot::BeastMart ? TownScene::BeastMart : TownScene::Square;
    townPic = paintTown(painter_, scene, vpCols, vpRows, townSel_, static_cast<int>(now_ / 222), density);  // the flames: 222 ms a frame
    layers = townPic.layers;
    buildTownLabels(townPic.labels, vpCols, vpRows, term);
  } else if (portal_) {
    layers.blitOriginal(layers.objects, painter_.image("TownPortal-table-" + std::to_string(static_cast<int>(now_ / 100) % 6 + 1)), 82, 7);  // the original loops its 6 frames at 100 ms
  }
  const bool dry = tracker_.fountainDone(dungeon_.floor());
  const Minimap mini = mapPicture(dungeon_, me_, me_.x - kMinimapSize / 2, me_.y - kMinimapSize / 2, kMinimapSize, kMinimapSize, dry);
  MapPicture fullMap;
  if (mapOpen_ && !inTown_) fullMap = mapPicture(dungeon_, me_, 1, 1, tuning().dungeon.sizeX, tuning().dungeon.sizeY, dry);
  HudInfo hud;
  hud.name = hero_.name;
  hud.level = hero_.level;
  hud.floor = dungeon_.floor();
  hud.place = inTown_ ? "Town" : "Dungeon " + std::to_string(dungeon_.floor());
  hud.hp = std::max(0, hero_.hp);
  hud.maxHP = hero_.maxHP;
  hud.hpHurt = hpHurt_;
  hud.hpBefore = hurtFrom_;
  hud.mp = hero_.mp;
  hud.maxMP = hero_.maxMP;
  hud.ac = hero_.ac;
  hud.gold = hero_.gold;
  hud.xp = hero_.experience;
  hud.xpNeeded = Hero::expNeeded(hero_.level);
  hud.score = hero_.score;
  for (size_t i = 0; i < 5; ++i) hud.stats[i] = static_cast<int>(std::floor(hero_.calc[i]));
  for (const Monster& c : hero_.charmees) hud.charmees.push_back({c.displayName, c.level, c.hp, c.maxHP});
  OverlayBox overlay;
  if (chest_ && chest_->open) {
    const Item& it = chest_->tr.item;
    if (it.isEquipment()) {
      const auto& worn = hero_.equipment[static_cast<size_t>(it.type)];
      overlay.columns.push_back(itemColumn(worn ? *worn : emptyItem(it.type), "Equipped item", chest_->pick == 0 ? "[Enter/Z] KEEP THIS" : "", chest_->pick == 0));
      overlay.columns.push_back(itemColumn(it, "In the chest", chest_->pick == 1 ? "[Enter/Z] TAKE THIS" : "", chest_->pick == 1));
    } else if (it.type == EquipmentType::Scroll) {
      OverlayColumn c = itemColumn(it, "A scroll!", hero_.hasScroll(it.scroll) ? "Already owned! [Enter/Z]" : "[Enter/Z] TAKE THIS", true, &hero_);
      c.scroll = scrollArt_;
      c.scrollT = animate_ ? static_cast<double>(now_ - chest_->openedAt) / 1000.0 : 3.0;  // (without a clock: the settled picture)
      overlay.columns.push_back(std::move(c));
    } else {
      OverlayColumn c;
      c.selected = true;
      c.title = it.type == EquipmentType::Health_Potion ? "Instant Health!" : "Instant Mana!";
      c.lines.push_back({std::string("You've ") + (it.type == EquipmentType::Health_Potion ? "healed " : "restored ") + std::to_string(chest_->restored) +
                             (it.type == EquipmentType::Health_Potion ? " HP!" : " MP!"),
                         "", 1});
      c.footer = "[Enter/Z] OK";
      c.orb = it.type == EquipmentType::Health_Potion ? 1 : 2;
      c.orbT = animate_ ? static_cast<double>(now_ - chest_->openedAt) / 1000.0 : 2.0;  // (without a clock: the settled orb)
      overlay.columns.push_back(c);
    }
  }
  if (secret_ && secret_->phase == SecretDoor::Phase::Offer) {  // SecretDoor.lua CreateFountainOffer
    OverlayColumn c;
    c.selected = true;
    c.title = "Fountain";
    c.lines.push_back({"A hidden FOUNTAIN!", "", 1});
    const std::string color = Fountain::colorName(fountain_.color());
    if (fountain_.known()) {
      c.lines.push_back({"You've seen " + color + " before...", "", 0});
      c.lines.push_back({Fountain::effectText(fountain_.effect()), "", 5});
    } else {
      c.lines.push_back({"You see mysterious " + color, "", 0});
      c.lines.push_back({"liquid circling the basin...", "", 0});
    }
    c.lines.push_back({"", "", 0});
    c.lines.push_back({"...Take a drink?", "", 1});
    c.buttons = {"Drink", "Pass"};
    c.buttonSel = fountainSel_;
    overlay.columns.push_back(c);
  }
  if (esp_ && combat_) {
    const Monster& m = combat_->monster();
    OverlayColumn c;
    c.title = "ESP";
    c.selected = true;
    c.lines.push_back({m.displayName + " (" + std::to_string(m.level) + ")", "", 1});
    c.lines.push_back({"HP", std::to_string(std::max(0, m.hp)) + "/" + std::to_string(m.maxHP), 0});
    c.lines.push_back({"Armor", std::to_string(m.ac), 0});
    for (size_t k = 0; k < 5; ++k) c.lines.push_back({statLongName(k), std::to_string(static_cast<int>(std::floor(m.stats[k]))), 0});
    c.footer = "[Any key] Close";
    overlay.columns.push_back(c);
    overlay.smallW = 36;
    overlay.smallH = 22;
  }
  MenuPanel menu;
  if (menu_ == Menu::Scrolls) {
    menu.place = MenuPanel::Place::Left;
    menu.title = "SCROLLS";
    for (const Item& sc : hero_.scrolls) menu.rows.push_back({sc.name, "MP " + std::to_string(sc.mpCost), 0, scrollBlocked(sc).empty()});
    menu.selected = std::clamp(subSel_, 0, std::max(0, static_cast<int>(hero_.scrolls.size()) - 1));
    if (!hero_.scrolls.empty()) {
      const Item& sc = hero_.scrolls[static_cast<size_t>(menu.selected)];
      const std::string why = scrollBlocked(sc);
      menu.detail.push_back({"MP " + std::to_string(sc.mpCost) + "   Lv " + std::to_string(sc.levelReq) + "   INT " + std::to_string(sc.intReq), "", 0});
      menu.detail.push_back({sc.utility ? "Outside fights" : "Fights only", "", 2});
      std::string desc;
      for (const std::string& d : sc.desc) {
        const size_t at = d.find_first_not_of(' ');
        if (at != std::string::npos) desc += (desc.empty() ? "" : " ") + d.substr(at);
      }
      for (const std::string& l : wrap(desc, 25)) menu.detail.push_back({l, "", 0});
      menu.footer = why.empty() ? "[Enter/Z] Cast  [Esc] Back" : why + ".  [Esc] Back";
      menu.footerTone = why.empty() ? 2 : 4;
    }
  } else if (menu_ == Menu::Inventory) {
    menu.place = MenuPanel::Place::Wide;
    menu.title = "INVENTORY";
    menu.listW = 60;
    fillInventoryMenu(menu, hero_, subSel_);
  }
  std::string hints, bottomHints;
  if (elevator_ && term) {  // the chosen floor alone, in big letters, with one line under it (the floors are cranked through with Up/Down)
    const int sel = elevator_->level;
    OverlayColumn c;
    c.selected = true;
    c.title = "ELEVATOR";
    c.bigTitle = sel == 0 ? "Town" : "Level " + std::to_string(sel);
    if (sel == 0) c.lines.push_back({"Enter: to town", "", 2});
    else if (tracker_.bossDone(sel)) c.lines.push_back({"Cleared: no way in", "", 4});
    else c.lines.push_back({"Enter: get off here", "", 1});
    overlay.columns.push_back(c);
    overlay.smallW = 28;
    overlay.smallH = 6;
    hints = "[Up/Down] Ride  [Enter/Z] Exit";
  } else if (elevator_) {
    OverlayColumn c;
    c.selected = true;
    c.title = elevator_->level == 0 ? "TOWN" : "Level " + std::to_string(elevator_->level);
    if (elevator_->level > 0 && tracker_.bossDone(elevator_->level)) c.lines.push_back({"CLEARED", "", 4});
    else if (elevator_->level == 0) c.lines.push_back({"Replenish!", "", 2});
    else if (elevator_->level == dungeon_.floor()) c.lines.push_back({"You are here", "", 2});
    overlay.columns.push_back(c);
    overlay.smallW = 28;
    overlay.smallH = 7;
    hints = "[Up/Down] Ride  [Enter/Z] Exit";
  } else if (inTown_) {
    switch (spot_) {
      case TownSpot::Sign: hints = "[Arrows] Choose  [Enter/Z] Go  [Esc] Menu"; break;
      case TownSpot::Temple: hints = "[Enter/Z] Heal  [Esc] Back"; break;
      case TownSpot::Blacksmith: hints = shopDetail_ ? "[Arrows] Next item  [Enter/Z] Buy  [Esc] Back" : "[Arrows] Choose  [Enter/Z] Details  [Esc] Back"; break;
      case TownSpot::BeastMart: hints = sellArmed_ ? "[Left/Right] Choose  [Enter/Z] Confirm  [Esc] No" : "[Arrows] Choose  [Enter/Z] Sell  [Esc] Back"; break;
    }
  } else if (portal_) {
    OverlayColumn c;
    c.selected = true;
    c.title = "Town portal";
    c.lines = {{"Leave for town?", "", 1}};
    c.buttons = {"Town", "Stay"};
    c.buttonSel = portalSel_;
    overlay.columns.push_back(c);
    overlay.smallW = 28;
    overlay.smallH = 11;
    hints = "[Left/Right] Choose  [Enter/Z] Confirm  [Esc] Stay";
  }
  CombatHud ch;
  if (combat_) {
    const Monster& m = combat_->monster();
    ch.name = m.displayName;
    ch.level = m.level;
    ch.hp = m.hp;
    ch.maxHP = m.maxHP;
    ch.ac = m.ac;
    for (size_t i = 0; i < 5; ++i) ch.stats[i] = static_cast<int>(std::floor(m.stats[i]));
    ch.image = monsterImage(m.image);
    ch.hover = m.hover;
    ch.boss10 = m.isBoss10;
    ch.pulse = 0.5 + 0.5 * std::sin(static_cast<double>(now_) / 380.0);
    ch.selected = menuSel_;
    if (specialAt_ != 0 && now_ >= specialAt_ && now_ < specialAt_ + kBannerMs) {
      static const char* kBanner[] = {"", "LIFE DRAIN!", "BITE!", "FIREBALL!", "LULLABY!", "SAND IN THE EYES!"};
      ch.banner = kBanner[std::clamp(specialKind_, 0, 5)];
      ch.bannerT = static_cast<double>(now_ - specialAt_) / kBannerMs;
      ch.bannerKind = specialKind_;
    }
    if (dying_) {
      ch.banner = "YOU DIED";
      ch.bannerHint = "Press Enter to continue";
      ch.bannerT = static_cast<double>(now_ - dyingAt_) / 1000.0;
      ch.bannerKind = 0;
      ch.over = true;  // the action grid goes away
    }
    {
      const Ailments& a = combat_->ailments();
      const auto add = [&](const char* n, int t) { if (t > 0) ch.status += (ch.status.empty() ? "" : "  ") + std::string(n) + " " + std::to_string(t); };
      add("POISONED", a.poison);
      add("ASLEEP", a.sleep);
      add("BLIND", a.blind);
    }
    for (int i = 0; i < kCombatActionCount; ++i) ch.enabled[static_cast<size_t>(i)] = combat_->available(static_cast<CombatAction>(i));
  }
  if (perk_) hints = "[Arrows] Choose  [Enter/Z] Choose perk  [Esc] Menu";
  else if (esp_) hints = "[Any key] Close";
  else if (menu_ == Menu::Scrolls) hints = "[Arrows] Choose  [Enter/Z] Cast  [Esc] Back";
  else if (menu_ == Menu::Inventory) hints = "[Arrows] Choose  [Esc] Back";
  if (hints.empty()) {  // the keys for what you can do now (the elevator, town and portal set their own above)
    if (dying_) hints = "[Enter/Z] Continue";
    else if (combat_) hints = "[Arrows] Choose  [Enter/Z] Use";
    else if (secret_) hints = secret_->phase == SecretDoor::Phase::Offer ? "[Left/Right] Choose  [Enter/Z] Select  [Esc] Back" : "";
    else if (chest_) hints = chest_->open ? (chest_->tr.item.isEquipment() ? "[Left/Right] Choose  [Enter/Z] Confirm" : "[Enter/Z] OK") : "[Enter/Z] Open";
    else {
      hints = "[Arrows] Move/Turn";
      bottomHints = "[S] Scrolls  [I/C] Inventory  [M] Map  [A] Audio: " + std::string(soundModeLabel(settings()));
    }
#ifdef LABYTERM_DEBUG_KEYS
    if (!combat_ && !chest_) hints += std::string("  [G] DEBUG monsters ") + (noSpawns_ ? "OFF" : "ON") + "  [O/P] orb chest  [L] level up";
#endif
    hints += "  [Esc] Menu";
  }
  MainScreenOptions opt;
  opt.view = &layers;
  if (inTown_ && !elevator_) opt.labels = &townPic.labels, opt.townSprites = &townPic.sprites;
  opt.torch = focused_ && torchWanted();
  opt.torchShift = torch_.shift();
  opt.torchPulse = torch_.pulse();
  opt.torchBreath = torch_.breath();
  opt.minimap = inTown_ ? nullptr : &mini;
  if (mapOpen_ && !inTown_) {
    opt.fullMap = &fullMap;
    opt.floorNumber = dungeon_.floor();
    hints = "[M/Esc] Close the map";
  }
  std::vector<OverlayLine> guide;
  if (inTown_ && !elevator_) {
    buildTownGuide(guide);
    opt.guide = &guide;
  }
  const int bossEvery = tuning().monsters.boss10EveryFloors;
  const bool boss10Floor = bossEvery > 0 && dungeon_.floor() % bossEvery == 0 && !inTown_ && !elevator_ && !tracker_.bossDone(dungeon_.floor());
  lairGlow_ = boss10Floor && (lairInView || bossIntro_);
  opt.bossGlow = lairGlow_;
  opt.bossGlowPulse = static_cast<float>(0.5 + 0.5 * std::sin(static_cast<double>(now_) / 380.0));
  opt.log = &log_;
  opt.hud = &hud;
  opt.combat = combat_ && !bossIntro_ ? &ch : nullptr;
  opt.overlay = overlay.columns.empty() ? nullptr : &overlay;
  MenuPanel perkPanel;
  if (perk_) buildPerkPanel(perkPanel);
  MenuPanel sysPanel;
  if (sys_.open()) {
    if (term) sys_.setPageChars(std::min(tl.vpCols, 84) - 6);  // a Help page's text fills its panel (84 cells at most, less the margins)
    sys_.build(sysPanel);
    if (term && sysPanel.place == MenuPanel::Place::Left) sysPanel.place = MenuPanel::Place::Dialog;  // (the terminal build centres it)
    hints = sys_.hints();
  }
  opt.menu = sys_.open() ? &sysPanel : perk_ ? &perkPanel : menu_ != Menu::None && !chest_ ? &menu : nullptr;
  opt.hints = hints;
  if (!mapOpen_ && !sys_.open()) opt.bottomHints = bottomHints;
  opt.statusTag = statusTag;
  if (term) drawMainScreenTerm(grid, theme, opt);
  else drawMainScreen(grid, theme, opt);
}

}  // namespace laby
