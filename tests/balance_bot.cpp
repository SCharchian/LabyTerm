// Balance harness, phase 1 (measure only): a bot plays COMPLETE runs on the real game (WalkSession, which owns the rules for
// encounters, chests, the elevator, the Temple and the Blacksmith) by pressing keys, and writes what happened as CSV tables.
// tools/balance_report.py runs many of these in parallel processes and turns the tables into build/balance/REPORT.md.
// Not a ctest (it takes minutes); build it with `tools/build.sh --target balance_bot`.
//
//   balance_bot --policy balanced --seeds 1 500 --out build/balance/raw/balanced.0 [--tuning file.json] [--max-floor 25]
//               [--set explore=100 --set townBelow=0.5 ...]   (any Policy field below)
//
// Everything that reads game state lives in the ADAPTER section (a friend of WalkSession plus formulas copied from Combat), so a
// change to Combat / Hero / Monster / WalkSession breaks the build or the numbers in exactly one place.
//
// Bot assumptions: the bot knows the whole map (no exploring beyond `explore` random steps
// per floor), never uses secret doors/fountains, Wizard Eye, Invisibility, ESP, Feeble Mind, and never sells charmees.
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <random>
#include <string>
#include <vector>

#include "ui/WalkSession.hpp"

namespace laby {

// ===================================================================================================================
// ADAPTER: everything the bot reads from or assumes about the game. Keep the rest of the file free of game internals.
// ===================================================================================================================
struct BalanceProbe {
  WalkSession& s;
  explicit BalanceProbe(WalkSession& w) : s(w) {}

  // --- state (WalkSession privates)
  Hero& hero() const { return s.hero_; }
  // Character creation's Re-Roll, as a careful player does it: until at least `needHigh` stats are above 15 and none is under 8.
  void rerollStart(int needHigh, int needDex = 0, int needCha = 0) const {
    for (int i = 0; i < 100000; ++i) {
      int high = 0, low = 0;
      for (double b : s.hero_.base) { high += b > 15; low += b < 8; }
      if (needDex > 0 || needCha > 0) {  // a player with a build in mind: a specific DEX and CHA
        if (s.hero_.base[static_cast<size_t>(Stat::DEX)] >= needDex && s.hero_.base[static_cast<size_t>(Stat::CHA)] >= needCha) return;
      } else if (high >= needHigh && low == 0) {
        return;
      }
      s.hero_.rollStats(s.rng_);
      s.hero_.rollStartingItem(s.rng_);
    }
  }
  const Dungeon& dungeon() const { return s.dungeon_; }
  const Walker& me() const { return s.me_; }
  int floor() const { return s.dungeon_.floor(); }
  bool onMap() const { return s.onMap_; }
  bool gameOver() const { return s.dead_ || s.mode_ == WalkSession::Mode::GameOver; }
  bool inCombat() const { return s.combat_.has_value(); }
  const Monster& monster() const { return s.combat_->monster(); }
  bool perk() const { return s.perk_; }
  int perkSel() const { return s.perkSel_; }
  bool esp() const { return s.esp_; }
  bool secret() const { return s.secret_.has_value(); }
  FountainEffect fountainEffect() const { return s.fountain_.effect(); }
  bool fountainKnown() const { return s.fountain_.known(); }
  bool debugGoNextTo(TileClass t) const { return s.debugGoNextTo(t); }
  bool menuOpen() const { return s.menu_ != WalkSession::Menu::None; }
  bool chest() const { return s.chest_.has_value(); }
  bool chestOpen() const { return s.chest_->open; }
  bool chestLevel() const { return s.chest_->levelChest; }
  bool chestBoss() const { return s.chest_->bossChest; }
  int chestPick() const { return s.chest_->pick; }
  const Item& chestItem() const { return s.chest_->tr.item; }
  int chestGold() const { return static_cast<int>(std::floor(s.chest_->tr.gold)); }
  bool portal() const { return s.portal_; }
  bool inTown() const { return s.inTown_; }
  int townSel() const { return s.townSel_; }
  bool atSign() const { return s.spot_ == WalkSession::TownSpot::Sign; }
  bool atBlacksmith() const { return s.spot_ == WalkSession::TownSpot::Blacksmith; }
  bool atBeastMart() const { return s.spot_ == WalkSession::TownSpot::BeastMart; }
  bool shopDetail() const { return s.shopDetail_; }
  const std::vector<Item>& shop() const { return s.shop_; }
  bool elevator() const { return s.elevator_.has_value(); }
  int elevatorLevel() const { return s.elevator_->level; }
  bool bossDone(int f) const { return s.tracker_.bossDone(f); }
  bool treasureDone(int f) const { return s.tracker_.treasureDone(f); }
  const std::optional<RunSummary>& summary() const { return s.summary_; }
  bool standingOnElevator() const { return s.onMap_ && s.me_.standingOn == TileClass::ELEVATOR; }

  // --- what the bot presses (hotkeys of the combat menu: "acshtp")
  void key(Key k, char32_t ch = 0) { s.handle(k, ch); }

  // --- chances the bot estimates before it acts (the formulas of Combat::charm / trick / pray; only used to choose, never to resolve)
  static double minChance() { return tuning().actions.minChance; }
  static double charmChance(const Hero& h, const Monster& m) { return Combat::charmChance(h, m); }
  static double trickChance(const Hero& h, const Monster& m) { return Combat::trickChance(h, m); }
  static double prayChance(const Hero& h, const Monster& m) { return Combat::prayChance(h, m); }
  static int regenSteps() { return tuning().dungeon.regenSteps; }
  int dungeonW() const { return s.dungeon_.level().width() + 2; }  // tiles are 1-based (x 1..width), so the grid scanned is padded; floors grow with depth
  int dungeonH() const { return s.dungeon_.level().height() + 2; }
  static int maxFloor() { return tuning().dungeon.maxFloor; }
  static int itemCost(const Item& i) { return itemPrice(i); }
};

}  // namespace laby

using namespace laby;

namespace {

// ===================================================================================================================
// Policies
// ===================================================================================================================
struct Policy {
  std::string name = "balanced";
  double prayBelow = 0.25;     // pray when HP fraction is under this (and the prayer has a fair chance); 0 = never
  double prayMinChance = 0.15; // ... and the prayer's chance is at least this
  double hideBelow = 0.4;      // hide behind charmees when HP fraction is under this; 0 = never
  double townBelow = 0.4;      // walking: ride to the town to heal when HP fraction is under this; 0 = never
  double portal = 1;           // 1: always take the boss's town portal, 0: only when HP fraction < 0.5
  double temple = 1;           // 0: never use the Temple (ablation)
  double reroll = 2;           // character creation: re-roll until this many stats are above 15 (none under 8); 0 = take the first roll
  double bossHeal = 0;         // before walking to the boss, ride to the town to heal if HP is under this fraction (0 = never)
  double buyGear = 1;
  double buyScrolls = 1;
  double castDamage = 1;       // Magic Arrow / Magic Missile whenever there is MP
  double castSleep = 0;
  double castHoly = 1;         // Holy Water on the undead
  double trickMin = 0.5;       // Trick when its chance is at least this (>1: never)
  double charmMin = 0.5;       // Charm when its chance is at least this (>1: never)
  double levelPerFloor = 0;    // fight the boss / go down only with hero level >= floor * this + levelOffset (wander and fight until then)
  double levelOffset = 0;
  double grindMax = 1500;      // most random steps spent grinding per floor
  double explore = 100;        // random steps per floor before heading for the goals (a human does not know the map)
  double perkInt = 0;          // 1: level-up perks go to INT/VIT instead of STR/VIT/DEX
  double ratingInt = 0.5;      // weight of INT in the gear rating (scroll builds weigh it up)
  double perkCha = 0;          // 1: level-up perks go to CHA/VIT/DEX (the charm build)
  double ratingCha = 0;        // weight of CHA in the gear rating
  double style = 0;            // 1: the author's way of fighting (see doCombatTurn: Hide often, charm when attacking does not work, Holy Water on the undead, attack scrolls)
  double needDex = 0, needCha = 0;  // re-roll the character until DEX and CHA are at least this (0: use `reroll`)
  double perkRandom = 0;       // 1: every level-up takes "<Random> +2"
  double hideChance = 0;       // style 1: with a charmee, hide this often
  double keepAhead = 0;        // sell every charmee that is not at least this many levels above the hero (0: use sellBehind)
  double fountain = 0;         // 1: first thing on every floor, drink from its fountain (unless the colour is known to be bad)
  double masterBonus = 1;      // a master's piece in the Blacksmith's list counts this much more than its rating gain
  double minGain = 0;          // buy gear only if it raises the rating by at least this fraction
  double scrollInt = 99;       // style 1: cast attack scrolls from the first turn when INT is at least this
  double sellBehind = 0;       // sell a charmee at BeastMart once it is this many levels under the hero (0: never sell)
};

std::vector<Policy> allPolicies() {
  std::vector<Policy> v;
  Policy bal;
  bal.name = "balanced";
  v.push_back(bal);
  Policy hum = bal;
  hum.name = "human";  // what a careful human does: heals in town under 60% HP and always before the boss, prays at 35%, explores a bit more of each floor
  hum.townBelow = 0.6;
  hum.bossHeal = 0.95;
  hum.prayBelow = 0.35;
  hum.explore = 150;
  v.push_back(hum);
  Policy reck = bal;
  reck.name = "reckless";
  reck.prayBelow = reck.hideBelow = reck.townBelow = 0;
  reck.portal = 0;
  reck.buyScrolls = 0;
  reck.castDamage = reck.castHoly = 0;
  reck.trickMin = reck.charmMin = 2;
  reck.explore = 0;
  v.push_back(reck);
  Policy caut = bal;
  caut.name = "cautious";
  caut.prayBelow = 0.4;
  caut.hideBelow = 0.55;
  caut.townBelow = 0.6;
  caut.levelPerFloor = 0.8;
  v.push_back(caut);
  Policy scr = hum;
  scr.name = "scroll-heavy";
  scr.castSleep = 1;
  scr.trickMin = 0.35;
  scr.perkInt = 1;
  scr.ratingInt = 2.5;
  v.push_back(scr);
  Policy gr = caut;
  gr.name = "grinder";  // a patient human: heals early, and levels up on every floor (hero level >= floor) before the boss
  gr.townBelow = 0.5;
  gr.bossHeal = 0.95;
  gr.levelPerFloor = 1.0;
  gr.levelOffset = 1;
  gr.grindMax = 4000;
  v.push_back(gr);
  Policy sam = hum;
  sam.name = "sam";  // the author's rules: re-roll for DEX 17+ and CHA 15+, fountain first, then the treasure and the boss, town after every boss (sell charmees unless 2+ levels above the hero, better gear, the scroll),
                     // Hide often, charm when attacking is not working, Holy Water on the undead, attack scrolls, level-ups always "<Random> +2"
  sam.style = 1;
  sam.needDex = 17;
  sam.needCha = 15;
  sam.perkRandom = 1;
  sam.hideChance = 0.7;
  sam.keepAhead = 2;
  sam.fountain = 1;
  sam.masterBonus = 1.3;
  sam.minGain = 0.04;
  sam.scrollInt = 20;
  sam.charmMin = 0.25;
  sam.ratingCha = 1.0;
  sam.explore = 250;
  v.push_back(sam);
  Policy chm = hum;
  chm.name = "charmer";  // the charm build: CHA-weighted gear, charms almost everything (35%+), hides behind charmees under half HP, sells the outgrown ones at BeastMart
  chm.charmMin = 0.35;
  chm.hideBelow = 0.5;
  chm.perkCha = 0;
  chm.ratingCha = 1.5;
  chm.sellBehind = 2;
  v.push_back(chm);
  Policy nt = bal;
  nt.name = "balanced-notemple";  // ablation: no healing in town at all (the portal and shopping still happen)
  nt.temple = 0;
  v.push_back(nt);
  Policy oc = bal;
  oc.name = "orig-compare";  // a simplified baseline run: attack only, no shopping, no scrolls, portal + Temple after each boss
  oc.prayBelow = oc.hideBelow = oc.townBelow = 0;
  oc.buyGear = oc.buyScrolls = 0;
  oc.reroll = 0;
  oc.castDamage = oc.castHoly = oc.castSleep = 0;
  oc.trickMin = oc.charmMin = 2;
  v.push_back(oc);
  return v;
}

using Field = std::pair<const char*, double Policy::*>;
const std::vector<Field>& policyFields() {
  static const std::vector<Field> f = {
      {"prayBelow", &Policy::prayBelow},     {"prayMinChance", &Policy::prayMinChance}, {"hideBelow", &Policy::hideBelow},   {"townBelow", &Policy::townBelow},
      {"portal", &Policy::portal},           {"temple", &Policy::temple},               {"reroll", &Policy::reroll},         {"bossHeal", &Policy::bossHeal},         {"style", &Policy::style},           {"needDex", &Policy::needDex},       {"needCha", &Policy::needCha},       {"perkRandom", &Policy::perkRandom}, {"hideChance", &Policy::hideChance}, {"keepAhead", &Policy::keepAhead},     {"fountain", &Policy::fountain},     {"masterBonus", &Policy::masterBonus}, {"minGain", &Policy::minGain},   {"scrollInt", &Policy::scrollInt},         {"buyGear", &Policy::buyGear},       {"buyScrolls", &Policy::buyScrolls},
      {"castDamage", &Policy::castDamage},   {"castSleep", &Policy::castSleep},         {"castHoly", &Policy::castHoly},     {"trickMin", &Policy::trickMin},
      {"charmMin", &Policy::charmMin},       {"levelPerFloor", &Policy::levelPerFloor}, {"levelOffset", &Policy::levelOffset}, {"grindMax", &Policy::grindMax},     {"explore", &Policy::explore},
      {"perkInt", &Policy::perkInt},         {"ratingInt", &Policy::ratingInt},         {"perkCha", &Policy::perkCha},       {"ratingCha", &Policy::ratingCha},   {"sellBehind", &Policy::sellBehind}};
  return f;
}

// ===================================================================================================================
// Output tables (CSV, one process appends its rows; the Python side concatenates)
// ===================================================================================================================
struct Sink {
  std::string runs, fights, floors, events;
  bool brief = false;  // --brief: only the runs table (very long runs would make the others huge)
  void row(std::string& s, const char* fmt, ...) {
    if (brief && &s != &runs) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    s += buf;
    s += '\n';
  }
};

const char* kRunsHeader = "policy,seed,result,max_floor,death_floor,floors_cleared,hero_level,killer,killer_class,killer_boss,killer_level,steps,keys,fights,score,gold_earned,gold_spent,gold_end,temple_visits,temple_hp,temple_mp,regen_hp,potion_hp,levelup_hp,prayers,town_trips,buys";
const char* kFightsHeader = "policy,seed,floor,hero_level,hero_maxhp,hero_hp0,hero_str,hero_dex,hero_ac,boss,monster,kind,prefix,class,m_level,m_maxhp,m_ac,outcome,turns,scrolls,hp_lost,hp_end,xp_gain,charmees,est_charm,est_trick,est_pray,hero_int,hero_cha,hero_vit,max_hit,m_str,m_dex,m_vit";
const char* kFloorsHeader = "policy,seed,floor,steps,fights,hp_lost,regen_hp,gold_earned,level_in,level_out,boss_killed,died,arrivals";
const char* kEventsHeader = "policy,seed,floor,type,name,a,b,c";

std::string csvq(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos) return s;
  std::string o = "\"";
  for (char c : s) { if (c == '"') o += '"'; o += c; }
  return o + "\"";
}

// ===================================================================================================================
// The bot
// ===================================================================================================================
struct FloorAcc {
  int steps = 0, fights = 0, hpLost = 0, regenHp = 0, goldEarned = 0, levelIn = 0, levelOut = 0, arrivals = 0;
  bool bossKilled = false, died = false;
};

struct FightAcc {
  bool active = false;
  Monster m;
  int floor = 0, heroLevel = 0, maxHp = 0, hp0 = 0, turns = 0, scrolls = 0, xp0 = 0;
  double str = 0, dex = 0, intl = 0, cha = 0, vit = 0;
  int ac = 0;
  size_t charmees0 = 0;
  int lastHp = -1, maxHit = 0;  // the most HP the hero lost between two of its own actions (one monster turn)
  double estCharm = 0, estTrick = 0, estPray = 0;  // the chances of the other actions at the start of the fight (as the bot estimates them)
};

enum class Ctx { Walk, Fight, Chest, Town, Perk, Other };

class Bot {
 public:
  Bot(const Policy& p, std::uint64_t seed, int maxFloor, int explore, Sink& out)
      : pol_(p), seed_(seed), maxFloor_(maxFloor), session_(seed), p_(session_), out_(out), rng_(static_cast<std::uint32_t>(seed * 2654435761u + 12345)), exploreBase_(explore) {}

  void run() {
    if (pol_.reroll > 0 || pol_.needDex > 0) p_.rerollStart(static_cast<int>(pol_.reroll), static_cast<int>(pol_.needDex), static_cast<int>(pol_.needCha));
    lastHp_ = p_.hero().hp;
    lastMp_ = p_.hero().mp;
    lastGold_ = p_.hero().gold;
    floorStat(p_.floor()).levelIn = p_.hero().level;
    floorStat(p_.floor()).arrivals = 1;
    startFloor(p_.floor());
    long lastProgress = 0;
    int lastSteps = 0, lastFights = 0, lastFloor = 1;
    while (keys_ < kMaxKeys) {
      if (p_.gameOver()) {
        if (fight_.active) endFight();
        result_ = "died";
        break;
      }
      if (fight_.active && !p_.inCombat()) endFight();
      if (p_.gameOver()) continue;
      if (maxReached_ >= maxFloor_ + 1) { result_ = "capped"; break; }
      if (!result_.empty()) break;
      step();
      // watchdog: something must happen (steps, fights, floors) within a few thousand keys
      if (p_.hero().steps != lastSteps || fights_ != lastFights || p_.floor() != lastFloor) {
        lastSteps = p_.hero().steps;
        lastFights = fights_;
        lastFloor = p_.floor();
        lastProgress = keys_;
      } else if (keys_ - lastProgress > 4000) {
        result_ = "stuck";
        if (std::getenv("BALANCE_DEBUG_MAP"))
          std::fprintf(stderr, "stuck seed %llu floor %d elevlvl %d first %d: perk %d chest %d portal %d town %d elev %d menu %d combat %d wantTown %d stage %d atSign %d blacksmith %d detail %d sel %d hp %d/%d gold %d\n",
                       static_cast<unsigned long long>(seed_), p_.floor(), p_.elevator() ? p_.elevatorLevel() : -1, firstUncleared(), p_.perk(), p_.chest(), p_.portal(), p_.inTown(), p_.elevator(), p_.menuOpen(), p_.inCombat(), wantTown_, townStage_, p_.atSign(),
                       p_.atBlacksmith(), p_.shopDetail(), p_.townSel(), p_.hero().hp, p_.hero().maxHP, p_.hero().gold);
        break;
      }
    }
    if (result_.empty()) result_ = "timeout";
    finish();
  }

 private:
  static constexpr long kMaxKeys = 400000;

  // ---------------------------------------------------------------- bookkeeping
  FloorAcc& floorStat(int f) { return floors_[f]; }

  void press(Key k, char32_t ch, Ctx ctx) {
    ++keys_;
    Hero& h = p_.hero();
    const int hp = h.hp, gold = h.gold, steps = h.steps;
    p_.key(k, ch);
    // the game queues sounds forever if nobody listens
    if ((keys_ & 255) == 0) session_.takeAudio();
    const int dHp = h.hp - hp, dGold = h.gold - gold;
    FloorAcc& fa = floorStat(p_.floor());
    if (ctx == Ctx::Walk && h.steps != steps) {
      ++fa.steps;
      if (dHp > 0) { regenHp_ += dHp; fa.regenHp += dHp; }
    } else if (ctx == Ctx::Chest) {
      if (dHp > 0) potionHp_ += dHp;
      if (dGold > 0) { goldEarned_ += dGold; fa.goldEarned += dGold; }
    } else if (ctx == Ctx::Perk && dHp > 0) {
      levelupHp_ += dHp;
    }
    if (p_.inCombat() && !fight_.active) beginFight();
  }

  void beginFight() {
    Hero& h = p_.hero();
    fight_ = FightAcc{};
    fight_.active = true;
    fight_.m = p_.monster();
    fight_.floor = p_.floor();
    fight_.heroLevel = h.level;
    fight_.maxHp = h.maxHP;
    fight_.hp0 = h.hp;
    fight_.str = h.stat(Stat::STR);
    fight_.dex = h.stat(Stat::DEX);
    fight_.intl = h.stat(Stat::INT);
    fight_.cha = h.stat(Stat::CHA);
    fight_.vit = h.stat(Stat::VIT);
    fight_.ac = h.ac;
    fight_.xp0 = h.experience;
    fight_.charmees0 = h.charmees.size();
    fight_.estCharm = fight_.m.cls == MonsterClass::UNDEAD ? 0 : BalanceProbe::charmChance(h, fight_.m);
    fight_.estTrick = BalanceProbe::trickChance(h, fight_.m);
    fight_.estPray = BalanceProbe::prayChance(h, fight_.m);
    ++fights_;
    ++floorStat(fight_.floor).fights;
  }

  void endFight() {
    Hero& h = p_.hero();
    const Monster& m = fight_.m;
    if (fight_.lastHp >= 0) fight_.maxHit = std::max(fight_.maxHit, fight_.lastHp - h.hp);  // the monster's last turn
    std::string outcome;
    if (p_.gameOver()) outcome = "died";
    else if (p_.inTown()) outcome = "prayed";
    else if (h.charmees.size() > fight_.charmees0) outcome = "charmed";
    else if (p_.chest()) outcome = "won";
    else outcome = "fled";
    if (outcome == "prayed") ++prayers_;
    std::string kind = m.displayName, prefix;
    for (const MonsterPrefix& pf : MonsterData::get().prefixes)
      if (m.displayName.rfind(pf.name + " ", 0) == 0) { prefix = pf.name; kind = m.displayName.substr(pf.name.size() + 1); break; }
    const int hpEnd = std::max(h.hp, 0);
    const int lost = fight_.hp0 - hpEnd;
    floorStat(fight_.floor).hpLost += lost;
    if (outcome == "died") floorStat(fight_.floor).died = true;
    if (m.isBoss && (outcome == "won" || outcome == "charmed" || outcome == "fled")) floorStat(fight_.floor).bossKilled = true;
    out_.row(out_.fights, "%s,%llu,%d,%d,%d,%d,%.0f,%.0f,%d,%d,%s,%s,%s,%s,%d,%d,%d,%s,%d,%d,%d,%d,%d,%zu,%.3f,%.3f,%.3f,%.0f,%.0f,%.0f,%d,%.0f,%.0f,%.0f", pol_.name.c_str(), static_cast<unsigned long long>(seed_),
              fight_.floor, fight_.heroLevel, fight_.maxHp, fight_.hp0, fight_.str, fight_.dex, fight_.ac, m.isBoss ? 1 : 0, csvq(m.displayName).c_str(), csvq(kind).c_str(),
              csvq(prefix).c_str(), std::string(toString(m.cls)).c_str(), m.level, m.maxHP, m.ac, outcome.c_str(), fight_.turns, fight_.scrolls, lost, hpEnd,
              h.experience - fight_.xp0, h.charmees.size(), fight_.estCharm, fight_.estTrick, fight_.estPray, fight_.intl, fight_.cha, fight_.vit, fight_.maxHit, m.stat(Stat::STR), m.stat(Stat::DEX), m.stat(Stat::VIT));
    fight_.active = false;
    if (outcome == "died") killerWasBossFloor_ = fight_.floor;
  }

  void event(const char* type, const std::string& name, double a = 0, double b = 0, double c = 0) {
    out_.row(out_.events, "%s,%llu,%d,%s,%s,%.2f,%.2f,%.2f", pol_.name.c_str(), static_cast<unsigned long long>(seed_), p_.floor(), type, csvq(name).c_str(), a, b, c);
  }

  void startFloor(int f) {
    floorStat(f).levelIn = floorStat(f).levelIn ? floorStat(f).levelIn : p_.hero().level;
    maxReached_ = std::max(maxReached_, f);
    explore_ = static_cast<int>(exploreBase_);
    grind_ = 0;
    unreachable_ = 0;
    plan_.clear();
    lastFloorSeen_ = f;
  }

  void finish() {
    Hero& h = p_.hero();
    for (auto& [f, a] : floors_) {
      if (p_.bossDone(f)) a.bossKilled = true;
      a.levelOut = a.levelOut ? a.levelOut : h.level;
      out_.row(out_.floors, "%s,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", pol_.name.c_str(), static_cast<unsigned long long>(seed_), f, a.steps, a.fights, a.hpLost,
                a.regenHp, a.goldEarned, a.levelIn, a.levelOut, a.bossKilled ? 1 : 0, a.died ? 1 : 0, a.arrivals);
    }
    int cleared = 0;
    for (int f = 1; f <= maxReached_; ++f) cleared += p_.bossDone(f);
    std::string killer = "-", kcls = "-";
    int kboss = 0, klevel = 0, deathFloor = 0;
    if (const auto& s = p_.summary(); s) {
      killer = s->killedBy;
      kboss = s->killerWasBoss;
      klevel = s->killerLevel;
      deathFloor = s->floor;
      for (const auto& list : MonsterData::get().byClass)
        for (const MonsterDef& d : list) {
          if (s->killedBy.size() >= d.displayName.size() && s->killedBy.compare(s->killedBy.size() - d.displayName.size(), d.displayName.size(), d.displayName) == 0)
            kcls = std::string(toString(d.cls));
        }
    }
    out_.row(out_.runs, "%s,%llu,%s,%d,%d,%d,%d,%s,%s,%d,%d,%d,%ld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", pol_.name.c_str(), static_cast<unsigned long long>(seed_), result_.c_str(),
              maxReached_, deathFloor, cleared, h.level, csvq(killer).c_str(), kcls.c_str(), kboss, klevel, h.steps, keys_, fights_, h.score, goldEarned_, goldSpent_, h.gold,
              templeVisits_, templeHp_, templeMp_, regenHp_, potionHp_, levelupHp_, prayers_, townTrips_, buys_);
  }

  // ---------------------------------------------------------------- the decision loop
  void step() {
    if (p_.esp()) return press(Key::Other, 0, Ctx::Other);
    if (p_.perk()) return doPerk();
    if (p_.secret()) {  // the fountain's offer (only a policy with `fountain` walks into the secret door)
      const FountainEffect e = p_.fountainEffect();
      const bool bad = p_.fountainKnown() && (e == FountainEffect::StatDown || e == FountainEffect::Alarm || e == FountainEffect::ShadowFight || e == FountainEffect::Teleport);
      backOut_ = true;
      if (pol_.fountain > 0 && !bad) { ++drinks_; return press(Key::Enter, 0, Ctx::Other); }
      return press(Key::Other, U'x', Ctx::Other);
    }
    if (p_.chest()) return doChest();
    if (p_.portal()) return doPortal();
    if (p_.inTown()) return doTown();
    if (p_.elevator()) return doElevator();
    if (p_.menuOpen()) return press(Key::Other, U'x', Ctx::Other);
    if (p_.inCombat()) return doCombatTurn();
    doWalk();
  }

  // ---------------------------------------------------------------- level-up perks
  void doPerk() {
    static const int kWarrior[] = {0, 3, 1}, kMage[] = {2, 3, 2, 1}, kCharm[] = {4, 3, 4, 1};
    const int want = pol_.perkRandom > 0 ? 5 : pol_.perkCha > 0 ? kCharm[perkCount_ % 4] : pol_.perkInt > 0 ? kMage[perkCount_ % 4] : kWarrior[perkCount_ % 3];
    if (p_.perkSel() != want) return press(Key::Down, 0, Ctx::Perk);
    ++perkCount_;
    event("levelup", "L" + std::to_string(p_.hero().level + 0), p_.hero().level, want);
    press(Key::Enter, 0, Ctx::Perk);
  }

  // ---------------------------------------------------------------- gear rating (the bot's own yardstick)
  double rating(const Hero& h) const {
    return 3 * h.stat(Stat::STR) + 2 * h.stat(Stat::DEX) + pol_.ratingInt * h.stat(Stat::INT) + pol_.ratingCha * h.stat(Stat::CHA) + 0.7 * h.maxHP + 3 * h.ac;
  }
  // Rating of the hero wearing `it`; a bot does not wear what costs it a fifth of its HP (items can push max HP to zero or below,
  // which the game allows and the report counts as an anomaly), so such items rate as -infinity.
  double ratingWith(const Item& it, bool note = false) {
    Hero h = p_.hero();
    h.equip(it);
    if (h.maxHP < 1 && note) event("item_zero_hp", it.displayName, h.maxHP, it.hp, it.level);
    if (h.maxHP < 1 || (h.maxHP < p_.hero().maxHP && h.maxHP < 0.8 * p_.hero().maxHP)) return -1e9;
    return rating(h);
  }

  // ---------------------------------------------------------------- chests
  void doChest() {
    const Item& it = p_.chestItem();
    if (!p_.chestOpen()) {
      if (it.isEquipment()) ratingWith(it, true);  // (notes an item that would zero the hero's HP)
      return press(Key::Enter, 0, Ctx::Chest);
    }
    if (it.isEquipment()) {
      const bool take = ratingWith(it) > rating(p_.hero()) + 0.01;
      if ((p_.chestPick() == 1) != take) return press(Key::Right, 0, Ctx::Chest);
      event(p_.chestLevel() ? "chest_level" : p_.chestBoss() ? "chest_boss" : "chest", it.displayName, take ? 1 : 0, it.level, p_.chestGold());
    } else {
      event(p_.chestLevel() ? "chest_level" : p_.chestBoss() ? "chest_boss" : "chest", it.name, 0, it.level, p_.chestGold());
    }
    press(Key::Enter, 0, Ctx::Chest);
  }

  void doPortal() {
    const Hero& h = p_.hero();
    const bool go = pol_.portal > 0 || h.hp < h.maxHP * 0.5;
    if (go) {
      wantTown_ = true;
      ++townTrips_;
      plan_.clear();
      return press(Key::Enter, 0, Ctx::Other);
    }
    press(Key::Other, U'b', Ctx::Other);
  }

  // ---------------------------------------------------------------- town
  void gotoSign(int sel) {  // the sign post's rows: 0 Temple, 1 Blacksmith, 2 BeastMart, 3 Labyrinth
    press(Key::Down, 0, Ctx::Town);
    (void)sel;
  }

  void doTown() {
    Hero& h = p_.hero();
    if (!wasInTown_) {
      wasInTown_ = true;
      townStage_ = 0;
      event("town", "arrive", h.hp, h.maxHP, h.gold);
    }
    if (p_.atSign()) {
      if (townStage_ == 0) {
        if (pol_.temple > 0 && (h.hp < h.maxHP || h.mp < h.maxMP)) {
          if (p_.townSel() != 0) return press(Key::Down, 0, Ctx::Town);
          templeHpBefore_ = h.hp;
          templeMpBefore_ = h.mp;
          return press(Key::Enter, 0, Ctx::Town);  // into the Temple
        }
        townStage_ = 1;
      }
      if (townStage_ == 1) {
        if ((pol_.buyGear > 0 || pol_.buyScrolls > 0) && h.gold >= 1 && shopsPossible()) {
          if (p_.townSel() != 1) return press(Key::Down, 0, Ctx::Town);
          shopSettled_ = false;
          return press(Key::Enter, 0, Ctx::Town);
        }
        townStage_ = 2;
      }
      if (townStage_ == 2) {
        if ((pol_.sellBehind > 0 || pol_.keepAhead > 0) && weakestCharmee() >= 0) {
          if (p_.townSel() != 2) return press(Key::Down, 0, Ctx::Town);
          return press(Key::Enter, 0, Ctx::Town);  // into BeastMart
        }
        townStage_ = 3;
      }
      if (p_.townSel() != 3) return press(Key::Down, 0, Ctx::Town);
      wasInTown_ = false;
      wantTown_ = false;
      fromTown_ = true;
      plan_.clear();
      return press(Key::Enter, 0, Ctx::Town);  // the elevator
    }
    if (p_.atBeastMart()) return doBeastMart();
    if (!p_.atBlacksmith()) {  // the Temple (the only other shop the bot enters)
      if (townStage_ == 0 && (h.hp < h.maxHP || h.mp < h.maxMP)) {
        const int dhp = h.maxHP - h.hp, dmp = h.maxMP - h.mp;
        press(Key::Enter, 0, Ctx::Town);
        ++templeVisits_;
        templeHp_ += dhp;
        templeMp_ += dmp;
        event("temple", "heal", dhp, dmp, h.gold);
        return;
      }
      townStage_ = 1;
      return press(Key::Escape, 0, Ctx::Town);
    }
    doShop();
  }

  bool shopsPossible() { return true; }

  // The index of the charmee to sell (the weakest one the hero has outgrown), or -1.
  int weakestCharmee() const {
    const Hero& h = p_.hero();
    int best = -1;
    for (size_t i = 0; i < h.charmees.size(); ++i)
      if ((pol_.keepAhead > 0 ? h.charmees[i].level < h.level + pol_.keepAhead : h.charmees[i].level + pol_.sellBehind <= h.level) && (best < 0 || h.charmees[i].level < h.charmees[static_cast<size_t>(best)].level)) best = static_cast<int>(i);
    return best;
  }

  void doBeastMart() {
    Hero& h = p_.hero();
    const int idx = weakestCharmee();
    if (idx < 0) {
      townStage_ = 3;
      return press(Key::Escape, 0, Ctx::Town);
    }
    if (p_.townSel() != idx) return press(Key::Down, 0, Ctx::Town);
    const Monster m = h.charmees[static_cast<size_t>(idx)];
    const int gold0 = h.gold;
    press(Key::Enter, 0, Ctx::Town);  // asks
    press(Key::Enter, 0, Ctx::Town);  // yes
    if (h.gold > gold0) {
      goldEarned_ += h.gold - gold0;
      ++sold_;
      event("sell", m.displayName, h.gold - gold0, m.level, h.gold);
    } else {
      press(Key::Escape, 0, Ctx::Town);
    }
  }

  // Blacksmith: buys what raises the rating (equipment) or what the policy wants (scrolls), best first, until nothing fits the purse.
  double scrollValue(const Item& it) const {
    if (pol_.buyScrolls <= 0 || p_.hero().hasScroll(it.scroll)) return 0;
    const Hero& h = p_.hero();
    if (h.level + 2 < it.levelReq || h.stat(Stat::INT) + 3 < it.intReq) return 0;
    switch (it.scroll) {
      case ScrollType::MagicMissile: return 30;
      case ScrollType::MagicArrow: return 18;
      case ScrollType::Sleep: return pol_.castSleep > 0 ? 14 : 0;
      case ScrollType::HolyWater: return pol_.castHoly > 0 ? 8 : 0;
      default: return 0;
    }
  }

  int bestBuy() {
    const Hero& h = p_.hero();
    const std::vector<Item>& shop = p_.shop();
    int best = -1;
    double bestGain = 0.01;
    const double base = rating(h);
    for (size_t i = 0; i < shop.size(); ++i) {
      const Item& it = shop[i];
      if (BalanceProbe::itemCost(it) > h.gold) continue;
      double gain = 0;
      if (it.type == EquipmentType::Scroll) gain = scrollValue(it);
      else if (pol_.buyGear > 0 && it.isEquipment()) {
        gain = ratingWith(it) - base;
        if (gain < pol_.minGain * base) gain = 0;  // not meaningfully better
        if (it.masterwork && gain > 0) gain *= pol_.masterBonus;
      }
      if (gain > bestGain) { bestGain = gain; best = static_cast<int>(i); }
    }
    return best;
  }

  void doShop() {
    Hero& h = p_.hero();
    if (p_.shopDetail()) {
      const int idx = p_.townSel();
      const Item it = p_.shop()[static_cast<size_t>(idx)];
      const int price = BalanceProbe::itemCost(it), gold0 = h.gold;
      const double gain = it.type == EquipmentType::Scroll ? scrollValue(it) : ratingWith(it) - rating(h);
      press(Key::Enter, 0, Ctx::Town);
      if (h.gold < gold0) {
        goldSpent_ += gold0 - h.gold;
        ++buys_;
        event("buy", it.displayName, price, gain, h.gold);
      } else {
        press(Key::Escape, 0, Ctx::Town);  // refused: back to the list
      }
      return;
    }
    if (!shopSettled_) {  // first look at this visit's stock
      shopSettled_ = true;
      for (const Item& it : p_.shop()) event("stock", it.displayName, BalanceProbe::itemCost(it), it.level, h.gold);
    }
    const int b = bestBuy();
    if (b < 0) {
      townStage_ = 2;
      return press(Key::Escape, 0, Ctx::Town);
    }
    if (p_.townSel() != b) return press(Key::Down, 0, Ctx::Town);
    press(Key::Enter, 0, Ctx::Town);
  }

  // ---------------------------------------------------------------- the elevator
  int firstUncleared() const {
    int f = 1;
    while (p_.bossDone(f)) ++f;
    return f;
  }

  void doElevator() {
    const int target = wantTown_ ? 0 : std::min(firstUncleared(), maxFloor_ + 1);
    const int lvl = p_.elevatorLevel();
    plan_.clear();
    if (lvl > target) return press(Key::Up, 0, Ctx::Other);
    if (lvl < target) return press(Key::Down, 0, Ctx::Other);
    const int before = p_.floor();
    press(Key::Enter, 0, Ctx::Other);
    if (target > 0 && !p_.elevator()) {
      const int f = p_.floor();
      FloorAcc& a = floorStat(f);
      if (f != before || fromTown_) ++a.arrivals;
      fromTown_ = false;
      if (!a.levelIn) a.levelIn = p_.hero().level;
      if (f != before) startFloor(f);
      plan_.clear();
    } else if (target == 0 && !p_.elevator()) {
      wasInTown_ = false;
    }
  }

  // ---------------------------------------------------------------- walking
  // (the elevator can stand in a corridor: stepping on it opens the shaft and Enter at the same floor puts you back, so it is a way through)
  bool passable(TileClass c) const { return c == TileClass::FLOOR || c == TileClass::O_DOOR || c == TileClass::C_DOOR || c == TileClass::ELEVATOR; }

  bool findTile(TileClass t, int& tx, int& ty) const {
    const Dungeon& d = p_.dungeon();
    for (int y = 0; y < p_.dungeonH(); ++y)
      for (int x = 0; x < p_.dungeonW(); ++x)
        if (d.peekTileType(x, y) == t) { tx = x; ty = y; return true; }
    return false;
  }

  // Shortest key sequence (turns and steps forward) to a tile of class `goal`; the other special tiles are walls on the way.
  bool pathTo(TileClass goal, std::deque<Key>& out) const {
    const Dungeon& d = p_.dungeon();
    const int W = p_.dungeonW(), H = p_.dungeonH();
    const Walker& me = p_.me();
    const auto id = [&](int x, int y, int f) { return (y * W + x) * 4 + f; };
    std::vector<int> prev(static_cast<size_t>(W * H * 4), -2);
    std::vector<char> how(static_cast<size_t>(W * H * 4), 0);  // 'L', 'R', 'U'
    std::deque<int> q;
    const int start = id(me.x, me.y, static_cast<int>(me.facing));
    prev[static_cast<size_t>(start)] = -1;
    q.push_back(start);
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    int found = -1;
    while (!q.empty() && found < 0) {
      const int cur = q.front();
      q.pop_front();
      const int f = cur % 4, pos = cur / 4, x = pos % W, y = pos / W;
      const auto push = [&](int nx, int ny, int nf, char h) {
        const int n = id(nx, ny, nf);
        if (prev[static_cast<size_t>(n)] != -2) return;
        prev[static_cast<size_t>(n)] = cur;
        how[static_cast<size_t>(n)] = h;
        q.push_back(n);
      };
      push(x, y, (f + 3) % 4, 'L');
      push(x, y, (f + 1) % 4, 'R');
      const int nx = x + dx[f], ny = y + dy[f];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
      const TileClass c = d.peekTileType(nx, ny);
      if (c == goal) {
        prev[static_cast<size_t>(id(nx, ny, f))] = cur;
        how[static_cast<size_t>(id(nx, ny, f))] = 'U';
        found = id(nx, ny, f);
        break;
      }
      if (passable(c)) push(nx, ny, f, 'U');
    }
    if (found < 0) return false;
    std::vector<char> rev;
    for (int n = found; prev[static_cast<size_t>(n)] != -1; n = prev[static_cast<size_t>(n)]) rev.push_back(how[static_cast<size_t>(n)]);
    out.clear();
    for (auto it = rev.rbegin(); it != rev.rend(); ++it) out.push_back(*it == 'L' ? Key::Left : *it == 'R' ? Key::Right : Key::Up);
    return true;
  }

  void wander() {
    const Dungeon& d = p_.dungeon();
    const Walker& me = p_.me();
    const Walker::Basis b = Walker::basisFor(me.facing);
    const TileClass ahead = d.peekTileType(me.x + b.ax, me.y + b.ay);
    if (passable(ahead) && (rng_() % 100) < 80) return press(Key::Up, 0, Ctx::Walk);
    press((rng_() & 1) ? Key::Left : Key::Right, 0, Ctx::Walk);
  }

  void followPlan(TileClass goal) {
    if (plan_.empty() || planGoal_ != goal) {
      planGoal_ = goal;
      if (!pathTo(goal, plan_)) {
        plan_.clear();
        if (unreachable_ == 0 && std::getenv("BALANCE_DEBUG_MAP"))
          for (int y = 0; y < p_.dungeonH(); ++y) { for (int x = 0; x < p_.dungeonW(); ++x) std::fputc(static_cast<char>(p_.dungeon().peekTileType(x, y)), stderr); std::fputc('\n', stderr); }
        if (++unreachable_ == 1) event("unreachable", std::string(1, static_cast<char>(goal)), p_.me().x, p_.me().y);
        if (unreachable_ > 300) result_ = "stuck";  // (a goal the map does not connect to: report it, do not wander forever)
        return wander();
      }
    }
    const Key k = plan_.front();
    plan_.pop_front();
    press(k, 0, Ctx::Walk);
  }

  void doWalk() {
    Hero& h = p_.hero();
    const int f = p_.floor();
    if (f != lastFloorSeen_) startFloor(f);
    if (!p_.onMap()) return;
    // heal in town when badly hurt
    const bool hurt = pol_.townBelow > 0 && pol_.temple > 0 && h.hp < h.maxHP && h.hp < h.maxHP * pol_.townBelow;
    if (hurt) {
      if (!wantTown_) { wantTown_ = true; ++townTrips_; }
    }
    if (wantTown_) {
      if (p_.standingOnElevator()) { plan_.clear(); return session_.debugRideElevator(); }
      return followPlan(TileClass::ELEVATOR);
    }
    if (backOut_) { backOut_ = false; plan_.clear(); return press(Key::Down, 0, Ctx::Walk); }
    if (explore_ > 0) { --explore_; return wander(); }
    int tx, ty;
    if (pol_.fountain > 0 && !fountainTried_.count(f)) {  // the author's first goal on a floor: the fountain behind the secret door
      fountainTried_.insert(f);
      if (p_.debugGoNextTo(TileClass::SECRET)) { plan_.clear(); return press(Key::Up, 0, Ctx::Walk); }
    }
    if (findTile(TileClass::TREASURE, tx, ty) && (unreachable_ < 20 || p_.bossDone(f))) return followPlan(TileClass::TREASURE);  // (a treasure behind the lair: fight the boss first)
    // grinding: wander (and so meet monsters) until the hero is at the level the policy wants for this floor, before the boss and before descending
    if (pol_.levelPerFloor > 0 && h.level < f * pol_.levelPerFloor + pol_.levelOffset && grind_ < pol_.grindMax) { ++grind_; return wander(); }
    if (!p_.bossDone(f) && findTile(TileClass::BOSS, tx, ty)) {
      if (pol_.bossHeal > 0 && pol_.temple > 0 && h.hp < h.maxHP * pol_.bossHeal && !wantTown_) { wantTown_ = true; ++townTrips_; plan_.clear(); return; }
      return followPlan(TileClass::BOSS);
    }
    // the floor is done
    if (p_.bossDone(f) || !findTile(TileClass::BOSS, tx, ty)) {
      if (p_.standingOnElevator()) { plan_.clear(); return session_.debugRideElevator(); }
      return followPlan(TileClass::ELEVATOR);
    }
    wander();
  }

  // ---------------------------------------------------------------- combat
  int scrollIndex(ScrollType t) const {
    const Hero& h = p_.hero();
    for (size_t i = 0; i < h.scrolls.size(); ++i) {
      const Item& s = h.scrolls[i];
      if (s.scroll != t) continue;
      if (!s.combat || h.level < s.levelReq || h.stat(Stat::INT) < s.intReq || h.mp < s.mpCost) return -1;
      return static_cast<int>(i);
    }
    return -1;
  }

  void doCombatTurn() {
    Hero& h = p_.hero();
    const Monster& m = p_.monster();
    ++fight_.turns;
    if (fight_.lastHp >= 0) fight_.maxHit = std::max(fight_.maxHit, fight_.lastHp - h.hp);
    fight_.lastHp = h.hp;
    const double f = static_cast<double>(h.hp) / std::max(1, h.maxHP);
    if (scrollMenu_) {  // the scroll list is open: walk to the chosen row and cast
      scrollMenu_ = false;
      if (!p_.menuOpen()) return;
    }
    if (pol_.style == 1) return doCombatTurnSam(h, m, f);
    if (pol_.prayBelow > 0 && f < pol_.prayBelow && BalanceProbe::prayChance(h, m) >= pol_.prayMinChance) return press(Key::Other, U'p', Ctx::Fight);
    if (pol_.hideBelow > 0 && f < pol_.hideBelow && !h.charmees.empty()) return press(Key::Other, U'h', Ctx::Fight);
    if (h.charmees.size() < 2 && m.cls != MonsterClass::UNDEAD && BalanceProbe::charmChance(h, m) >= pol_.charmMin) return press(Key::Other, U'c', Ctx::Fight);
    if (BalanceProbe::trickChance(h, m) >= pol_.trickMin) return press(Key::Other, U't', Ctx::Fight);
    int idx = -1;
    if (pol_.castHoly > 0 && m.cls == MonsterClass::UNDEAD) idx = scrollIndex(ScrollType::HolyWater);
    if (idx < 0 && pol_.castSleep > 0 && !m.has(StatusFlag::SLEEPING) && m.level >= h.level && m.hp > m.maxHP / 2) idx = scrollIndex(ScrollType::Sleep);
    if (idx < 0 && pol_.castDamage > 0) {
      idx = scrollIndex(ScrollType::MagicMissile);
      if (idx < 0) idx = scrollIndex(ScrollType::MagicArrow);
    }
    if (idx >= 0) return castScroll(idx);
    press(Key::Other, U'a', Ctx::Fight);
  }

  // The author's way of fighting: attack by default; Holy Water on the undead; Hide often while there are charmees; when attacking is not working
  // (hurt, or the fight drags on) or there is no charmee, try to charm; attack scrolls from the start with a high INT, else when things go badly.
  void doCombatTurnSam(Hero& h, const Monster& m, double f) {
    if (pol_.prayBelow > 0 && f < pol_.prayBelow && BalanceProbe::prayChance(h, m) >= pol_.prayMinChance) return press(Key::Other, U'p', Ctx::Fight);
    if (m.cls == MonsterClass::UNDEAD) {
      const int holy = scrollIndex(ScrollType::HolyWater);
      if (holy >= 0) return castScroll(holy);
    }
    if (!h.charmees.empty() && std::uniform_real_distribution<double>(0, 1)(rng_) < pol_.hideChance) return press(Key::Other, U'h', Ctx::Fight);
    const bool notWorking = fight_.turns >= 3 || f < 0.7;
    if (h.charmees.size() < 2 && m.cls != MonsterClass::UNDEAD && (h.charmees.empty() || notWorking) && BalanceProbe::charmChance(h, m) >= pol_.charmMin) return press(Key::Other, U'c', Ctx::Fight);
    if (BalanceProbe::trickChance(h, m) >= pol_.trickMin) return press(Key::Other, U't', Ctx::Fight);
    if (h.stat(Stat::INT) >= pol_.scrollInt || notWorking || f < 0.5) {
      int idx = scrollIndex(ScrollType::MagicMissile);
      if (idx < 0) idx = scrollIndex(ScrollType::MagicArrow);
      if (idx >= 0) return castScroll(idx);
    }
    press(Key::Other, U'a', Ctx::Fight);
  }

  void castScroll(int idx) {
    press(Key::Other, U'r', Ctx::Fight);  // opens the scroll list (row 0)
    if (!p_.menuOpen()) return;
    for (int i = 0; i < idx; ++i) press(Key::Down, 0, Ctx::Fight);
    press(Key::Enter, 0, Ctx::Fight);
    ++fight_.scrolls;
    if (p_.menuOpen()) press(Key::Other, U'x', Ctx::Fight);  // refused: close it (the next turn attacks)
  }

  // ---------------------------------------------------------------- data
  Policy pol_;
  std::uint64_t seed_;
  int maxFloor_;
  WalkSession session_;
  BalanceProbe p_;
  Sink& out_;
  std::mt19937 rng_;
  double exploreBase_;

  long keys_ = 0;
  int fights_ = 0, lastFloorSeen_ = 0, maxReached_ = 1, explore_ = 0, grind_ = 0, unreachable_ = 0, perkCount_ = 0, townStage_ = 0;
  int sold_ = 0, drinks_ = 0;
  bool backOut_ = false;  // just left the fountain: step back out of the doorway
  std::set<int> fountainTried_;
  int goldEarned_ = 0, goldSpent_ = 0, templeVisits_ = 0, templeHp_ = 0, templeMp_ = 0, regenHp_ = 0, potionHp_ = 0, levelupHp_ = 0, prayers_ = 0, townTrips_ = 0, buys_ = 0;
  int lastHp_ = 0, lastMp_ = 0, lastGold_ = 0, templeHpBefore_ = 0, templeMpBefore_ = 0, killerWasBossFloor_ = 0;
  bool fromTown_ = false, wantTown_ = false, wasInTown_ = false, shopSettled_ = false, scrollMenu_ = false;
  std::string result_;
  std::map<int, FloorAcc> floors_;
  FightAcc fight_;
  std::deque<Key> plan_;
  TileClass planGoal_ = TileClass::FLOOR;
};

bool appendFile(const std::string& path, const std::string& header, const std::string& body) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) return false;
  std::fputs(header.c_str(), f);
  std::fputc('\n', f);
  std::fputs(body.c_str(), f);
  std::fclose(f);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string policyName = "balanced", out = "balance_out", tuning;
  bool brief = false;
  long from = 1, to = 100;
  int maxFloor = 25;
  std::vector<std::pair<std::string, double>> sets;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--policy") policyName = next();
    else if (a == "--seeds") { from = std::atol(next()); to = std::atol(next()); }
    else if (a == "--out") out = next();
    else if (a == "--brief") brief = true;
    else if (a == "--tuning") tuning = next();
    else if (a == "--max-floor") maxFloor = std::atoi(next());
    else if (a == "--set") {
      const std::string kv = next();
      const size_t eq = kv.find('=');
      if (eq == std::string::npos) { std::fprintf(stderr, "--set name=value\n"); return 2; }
      sets.push_back({kv.substr(0, eq), std::atof(kv.c_str() + eq + 1)});
    } else if (a == "--list") {
      for (const Policy& p : allPolicies()) std::printf("%s\n", p.name.c_str());
      return 0;
    } else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
  }
  // The report measures the repo's defaults unless told otherwise: ignore the player's own ~/.config/labyterm/tuning.json.
  setenv("LABYTERM_TUNING", tuning.empty() ? "/nonexistent/none.json" : tuning.c_str(), 1);
  setenv("LABYTERM_SETTINGS", "/nonexistent/settings.json", 1);

  Policy pol;
  bool found = false;
  for (const Policy& p : allPolicies())
    if (p.name == policyName) { pol = p; found = true; }
  if (!found) { std::fprintf(stderr, "unknown policy %s (--list)\n", policyName.c_str()); return 2; }
  for (const auto& [k, v] : sets) {
    bool ok = false;
    for (const Field& fld : policyFields())
      if (k == fld.first) { pol.*(fld.second) = v; ok = true; }
    if (!ok) { std::fprintf(stderr, "unknown policy field %s\n", k.c_str()); return 2; }
  }
  Sink sink;
  sink.brief = brief;
  for (long seed = from; seed <= to; ++seed) {
    Bot bot(pol, static_cast<std::uint64_t>(seed), maxFloor, pol.explore, sink);
    bot.run();
  }
  const bool ok = appendFile(out + ".runs.csv", kRunsHeader, sink.runs) && appendFile(out + ".fights.csv", kFightsHeader, sink.fights) &&
                  appendFile(out + ".floors.csv", kFloorsHeader, sink.floors) && appendFile(out + ".events.csv", kEventsHeader, sink.events);
  if (!ok) { std::fprintf(stderr, "cannot write %s.*.csv\n", out.c_str()); return 1; }
  return 0;
}
