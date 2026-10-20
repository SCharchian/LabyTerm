#include "game/Combat.hpp"
#include "game/Scrolls.hpp"

#include <algorithm>
#include <cmath>

#include "core/RandomUtil.hpp"
#include "core/Tuning.hpp"

namespace laby {
namespace {

// Damage after a balance multiplier; a hit always does at least 1.
int scaled(double dmg, double mult) { return std::max(1, static_cast<int>(std::floor(dmg * mult + 1e-9))); }

}  // namespace

Combat::Combat(Hero& hero, Monster monster, Rng& rng, Log say, int floor)
    : hero_(hero), monster_(std::move(monster)), rng_(rng), log_(std::move(say)), floor_(floor) {}

// A chance kept within the original's 5% .. 95% (the top clamp only applies to attack rolls).
double Combat::chance(double c, bool capTop) const {
  const auto& a = tuning().actions;
  return capTop ? std::min(std::max(c, a.minChance), a.maxChance) : std::max(c, a.minChance);
}

bool Combat::available(CombatAction a, std::string* why) const {
  auto no = [&](const char* r) {
    if (why) *why = r;
    return false;
  };
  switch (a) {
    case CombatAction::Charm: return hero_.charmees.size() < 2 ? true : no("You already have 2 Charmees!");
    case CombatAction::Scroll: return !hero_.scrolls.empty() ? true : no("You don't have any Scrolls!");
    case CombatAction::Hide: return !hero_.charmees.empty() ? true : no("You don't have any Charmees to hide behind!");
    default: return true;
  }
}

void Combat::act(CombatAction a) {
  if (outcome_ != CombatOutcome::Ongoing) return;
  std::string why;
  if (!available(a, &why)) {
    say(why);
    return;
  }
  if (sleepsThrough()) return;
  bool monstersTurn = true, hiding = false;
  switch (a) {
    case CombatAction::Attack: playerAttack(); break;
    case CombatAction::Charm:
      charm();
      monstersTurn = !monster_.charmed;
      break;
    case CombatAction::Hide:
      hide();
      hiding = true;
      break;
    case CombatAction::Trick: trick(); break;
    case CombatAction::Pray: pray(); break;
    case CombatAction::Scroll: break;
  }
  finishTurn(monstersTurn, hiding);
}

// Asleep: the action is lost (the monster still answers).
bool Combat::sleepsThrough() {
  if (ail_.sleep <= 0) return false;
  say("You are fast asleep!");
  if (--ail_.sleep == 0) say("You wake up!");
  finishTurn(true, false);
  return true;
}

void Combat::finishTurn(bool monstersTurn, bool hiding) {
  if (outcome_ != CombatOutcome::Ongoing) return;
  if (monstersTurn && monster_.hp > 0) tickAilments();
  if (hero_.hp <= 0) {
    say("You have died!");
    outcome_ = CombatOutcome::Died;
    return;
  }
  if (monstersTurn && monster_.hp > 0) {
    monsterPhase_ = true;
    monsterTurn(hiding);
    monsterPhase_ = false;
    if (outcome_ == CombatOutcome::Ongoing && hero_.hp <= 0) {
      say("You have died!");
      outcome_ = CombatOutcome::Died;
    }
  }
}

// Scrolls.lua ReadScroll and the Do* functions for the scrolls used in a fight (Wizard Eye and Invisibility are cast outside one).
void Combat::cast(ScrollType type) {
  if (outcome_ != CombatOutcome::Ongoing) return;
  const auto at = std::find_if(hero_.scrolls.begin(), hero_.scrolls.end(), [&](const Item& s) { return s.scroll == type; });
  if (at == hero_.scrolls.end() || !at->combat) return;
  if (sleepsThrough()) return;
  const Item scroll = *at;
  const auto& t = tuning().scrolls;
  switch (payForScroll(hero_, rng_, scroll, floor_)) {
    case CastResult::NoMana:
      cue(Sfx::Bonk);
      say("Insufficient MP!");
      return;  // no action was taken
    case CastResult::Disintegrated:
      cue(Sfx::ScrollDisintegrated);
      say(scroll.name + " Scroll DISINTEGRATED!");
      break;
    case CastResult::Cast: {
      const bool undead = monster_.cls == MonsterClass::UNDEAD;
      const double pInt = hero_.stat(Stat::INT);
      const double delta = pInt - monster_.stat(Stat::INT), lvDelta = hero_.level - monster_.level;
      const auto hit = [&](int dmg) {
        monster_.hp -= dmg;
        say("You hit the " + monster_.displayName + " for " + std::to_string(dmg) + " damage!");
        if (monster_.hp <= 0) killed();
      };
      switch (type) {
        case ScrollType::MagicArrow: {
          cue(Sfx::MagicArrow);
          const std::int64_t lo = 1 + hero_.level, hi = std::max<std::int64_t>(lo, static_cast<std::int64_t>(std::floor(pInt)));
          int dmg = std::max(1, static_cast<int>(rng_.range(lo, hi)));
          if (undead) dmg *= t.arrowUndeadMultiplier;
          hit(dmg);
          break;
        }
        case ScrollType::HolyWater: {
          if (!undead) {
            cue(Sfx::ScrollFailed);
            say("Holy Water FAILED!  Not an Undead monster!");
            break;
          }
          if (rng_.real() <= t.holyWaterBaseChance + t.holyWaterPerIntDelta * delta + t.holyWaterPerLevelDelta * lvDelta) {
            cue(Sfx::HolyWater);
            say(monster_.displayName + " turned to ash!");
            monster_.hp = 0;
            killed();
          } else {
            cue(Sfx::ScrollFailed);
            say("Holy Water FAILED!");
          }
          break;
        }
        case ScrollType::Sleep: {
          double c = t.sleepBaseChance + t.sleepPerIntDelta * delta + t.sleepPerLevelDelta * lvDelta;
          if (undead) c = t.sleepUndeadChance;
          else if (monster_.cls == MonsterClass::HUMAN || monster_.cls == MonsterClass::ANIMAL) c *= t.sleepHumanAnimalMultiplier;
          if (c > rng_.real()) {
            cue(Sfx::Sleep);
            say(monster_.displayName + " falls asleep!");
            monster_.set(StatusFlag::SLEEPING);
            monster_.set(StatusFlag::UNWAKABLE);  // it can't wake for one turn
          } else {
            cue(Sfx::ScrollFailed);
            say("Sleep FAILED!");
          }
          break;
        }
        case ScrollType::MagicMissile: {
          cue(Sfx::MagicMissile);
          const std::int64_t lo = static_cast<std::int64_t>(std::ceil(pInt * t.missileMinIntFactor));
          const std::int64_t hi = std::max(lo, static_cast<std::int64_t>(std::floor(pInt * t.missileMaxIntFactor)));
          int dmg = std::max(1, static_cast<int>(rng_.range(lo, hi)));
          if (undead) dmg = static_cast<int>(std::floor(dmg * static_cast<double>(rng_.range(t.missileUndeadMinPercent, t.missileUndeadMaxPercent)) / 100));
          hit(dmg);
          break;
        }
        case ScrollType::FeebleMind: {
          cue(Sfx::FeebleMind);
          double& mi = monster_.stats[static_cast<size_t>(Stat::INT)];
          mi = std::max(1.0, mi - std::floor(pInt / t.feebleMindIntDivisor));
          say(monster_.displayName + " is enfeebled!");
          break;
        }
        case ScrollType::ESP:
          cue(Sfx::ESP);
          say("You concentrate on the " + monster_.displayName + "'s mind...");
          esp_ = true;
          break;
        default: break;
      }
      break;
    }
  }
  finishTurn(scroll.costsTurn, false);
}

void Combat::killed() {
  if (outcome_ != CombatOutcome::Ongoing) return;
  outcome_ = CombatOutcome::Won;
  const int exp = experienceFor(hero_, monster_);
  cue(Sfx::MonsterDies, static_cast<int>(monster_.cls), 250);
  say("You killed the " + monster_.displayName + "! (" + std::to_string(exp) + " EXP)");
  hero_.experience += exp;
  hero_.score += monster_.experience * (monster_.isBoss ? tuning().score.multiplierBossKilled : tuning().score.multiplierMonsterKilled);
}

void Combat::playerAttack() {
  const auto& c = tuning().combat;
  const auto& d = tuning().defense;
  const double delta = hero_.level - monster_.level;
  const double pDex = hero_.stat(Stat::DEX), pStr = hero_.stat(Stat::STR);

  double hit = c.baseToHit + c.toHitDexAcModifier * (pDex - monster_.ac) + c.toHitLevelModifier * delta +
               c.toHitDexDeltaModifier * (pDex - monster_.stat(Stat::DEX));
  hit += static_cast<double>(rng_.range(-c.hitRandomPercent, c.hitRandomPercent)) / 100;
  if (ail_.blind > 0) hit -= tuning().monsterActions.blindToHitPenalty;
  hit = chance(hit, true);
  if (!(rng_.real() <= hit)) {
    cue(Sfx::AttackMiss);
    say(ail_.blind > 0 ? "You swing blindly and MISS!" : "You MISSED!");
  } else {
    double lo = std::floor(statLevelNormalize(rng_, pStr * c.minDamageStrFactor, delta * c.minDamageLevelFactor));
    double hi = std::ceil(statLevelNormalize(rng_, pStr, delta));
    if (hi < lo) std::swap(lo, hi);
    const double critChance = c.critBaseChance + (c.critPerDexAdvantage * (pDex - monster_.stat(Stat::DEX)) + c.critPerStr * pStr);
    const bool crit = rng_.real() <= critChance;
    double mult = 1;
    if (crit)
      mult += biasedRandom(rng_, c.critBonusCenterPercent, c.critBonusRadiusPercent, c.critBonusStrength, c.critBonusMaxPercent, c.critBonusMinPercent) / 100;
    // (a single-value damage range just gives that value)
    const double span = std::floor(hi - lo);
    const double base = lo + (span >= 1 ? static_cast<double>(rng_.range(1, static_cast<std::int64_t>(span))) : 0);
    double dmg = std::floor(base * mult);
    dmg = std::ceil(dmg - monster_.ac * d.monsterAcWhenPlayerHits - monster_.stat(Stat::VIT) * d.monsterVitWhenPlayerHits);
    const int dealt = scaled(std::max(dmg, 1.0), c.playerDamageMultiplier);
    monster_.hp -= dealt;
    const auto& weapon = hero_.equipment[static_cast<size_t>(EquipmentType::Weapon)];
    cue(Sfx::AttackHit, static_cast<int>(weapon ? weapon->weaponType : WeaponType::BLUNT));
    say((crit ? "CRITICAL HIT: " : "You deal: ") + std::to_string(dealt) + " damage!");
    if (monster_.hp < 1) killed();
  }

  // The player's charmees attack too.
  for (Monster& ch : hero_.charmees) {
    if (ch.hp <= 0 || monster_.hp <= 0) continue;
    const double roll = (static_cast<double>(rng_.range(1, c.attackRollDie)) + ch.stat(Stat::DEX)) * c.charmeeAttackDexMultiplier;
    if (roll >= monster_.ac) {
      double dmg = std::floor((static_cast<double>(rng_.range(1, c.npcDamageDie)) + ch.stat(Stat::STR)) / c.npcDamageStrDivisor);
      dmg = std::floor(dmg - monster_.ac * d.monsterAcWhenCharmeeHits - monster_.stat(Stat::VIT) * d.monsterVitWhenCharmeeHits);
      const int dealt = scaled(dmg, c.playerDamageMultiplier);
      monster_.hp -= dealt;
      cue(Sfx::MonsterAttack, static_cast<int>(ch.cls), 200);
      say("-->Your " + ch.displayName + " deals: " + std::to_string(dealt));
      if (monster_.hp < 1) {
        say("Your " + ch.displayName + " killed the monster!");
        killed();
      }
    } else {
      cue(Sfx::AttackMiss, -1, 200);
      say("-->" + ch.displayName + " misses!");
    }
  }
}

namespace {
// What an average hero of this level has of one stat (heroAverage in gameplay.json).
double averageStat(Stat s, int level) {
  const auto& a = tuning().heroAverage;
  const double up = std::max(level - 1, 0);
  switch (s) {
    case Stat::INT: return a.intBase + a.intPerLevel * up;
    case Stat::CHA: return a.chaBase + a.chaPerLevel * up;
    default: return a.vitBase + a.vitPerLevel * up;
  }
}
double relative(double stat, double average) { return average > 0 ? stat / average - 1.0 : 0.0; }
}  // namespace

double Combat::charmChance(const Hero& hero, const Monster& monster) {
  const auto& c = tuning().charm;
  const double v = c.baseChance + c.relativeSlope * relative(hero.stat(Stat::CHA), averageStat(Stat::CHA, hero.level)) + c.perLevelDelta * (hero.level - monster.level);
  return std::clamp(v, c.minChance, c.maxChance);
}

double Combat::trickChance(const Hero& hero, const Monster& monster) {
  const auto& t = tuning().trick;
  const double v = t.baseChance + t.relativeSlope * relative(hero.stat(Stat::INT), averageStat(Stat::INT, hero.level)) + t.perLevelDelta * (hero.level - monster.level);
  return std::clamp(monster.isBoss ? v * t.bossFactor : v, t.minChance, t.maxChance);
}

double Combat::prayChance(const Hero& hero, const Monster& monster) {
  const auto& p = tuning().pray;
  const double stat = p.intShare * hero.stat(Stat::INT) + (1 - p.intShare) * hero.stat(Stat::VIT);
  const double avg = p.intShare * averageStat(Stat::INT, hero.level) + (1 - p.intShare) * averageStat(Stat::VIT, hero.level);
  const double hpFrac = hero.maxHP > 0 ? static_cast<double>(hero.hp) / hero.maxHP : 1.0;
  const double hpScale = std::clamp((1.0 - hpFrac) / std::max(1.0 - p.hpDesperationFloor, 1e-6), 0.0, 1.0);
  const double behind = std::clamp(static_cast<double>(monster.level - hero.level), 0.0, static_cast<double>(p.maxLevelBehind));
  const double v = p.relativeSlope * relative(stat, avg) + p.hpDesperation * hpScale + p.perLevelBehind * behind;
  return std::clamp(p.baseChance + v, p.baseChance, p.maxChance);
}

int Combat::experienceFor(const Hero& hero, const Monster& monster) {
  const auto& t = tuning().progression;
  const int above = hero.level - monster.level;
  double cut = 0;
  if (above >= 5) cut = t.xpBackoff5Percent;
  else if (above == 4) cut = t.xpBackoff4Percent;
  else if (above == 3) cut = t.xpBackoff3Percent;
  else if (above == 2) cut = t.xpBackoff2Percent;
  else if (above == 1) cut = t.xpBackoff1Percent;
  return static_cast<int>(std::floor(monster.experience * t.killExpMultiplier * (1.0 - cut / 100.0) + 1e-9));
}

// What a monster's raw damage becomes against `defense` (armor and VIT, already weighted): the original subtracts it (an armored hero shrugs hits
// off entirely until a stronger monster suddenly hits hard); with defense.proportional the defense is relative to the blow: raw * raw / (raw + defense * strength),
// so armor halves a blow as strong as the armor itself, and the damage ramps up smoothly with the monster's strength.
double Combat::mitigate(double raw, double defense) const {
  const auto& d = tuning().defense;
  if (d.proportional <= 0) return std::floor(raw - defense);
  if (raw <= 0) return 0;
  return std::floor(raw * raw / (raw + std::max(0.0, defense) * d.proportionalStrength));
}

// One monster hit never takes more than combat.maxHitPercent of the hero's max HP (0 = no limit; a boss's limit is combat.maxHitPercentBoss).
int Combat::capHit(int hurt) const {
  const auto& c = tuning().combat;
  const int pct = monster_.isBoss ? c.maxHitPercentBoss : c.maxHitPercent;
  if (pct <= 0) return hurt;
  return std::min(hurt, std::max(1, static_cast<int>(std::ceil(hero_.maxHP * pct / 100.0))));
}

double Combat::monsterDamageFactor() const {
  const auto& c = tuning().combat;
  return c.monsterDamageMultiplier * (monster_.isBoss ? c.bossDamageMultiplier : 1.0);
}

void Combat::charm() {
  if (monster_.cls == MonsterClass::UNDEAD) {
    cue(Sfx::CharmFailed, static_cast<int>(monster_.cls));
    say("You cannot charm the undead!");
    return;
  }
  const double c = charmChance(hero_, monster_);
  if (!(rng_.real() <= c)) {
    cue(Sfx::CharmFailed, static_cast<int>(monster_.cls));
    say("Charm failed!");
    return;
  }
  monster_.charmed = true;
  cue(Sfx::CharmSuccess, static_cast<int>(monster_.cls));
  say("Charm worked!  " + monster_.displayName + " joins your party!");
  hero_.charmees.push_back(monster_);
  const int exp = experienceFor(hero_, monster_);
  say("Charming the " + monster_.displayName + " earned you " + std::to_string(exp) + " Experience!");
  hero_.experience += exp;
  hero_.score += monster_.experience * (monster_.isBoss ? tuning().score.multiplierBossKilled : tuning().score.multiplierMonsterCharmed);
  outcome_ = CombatOutcome::Charmed;
}

void Combat::hide() {
  const auto& c = tuning().combat;
  const auto& d = tuning().defense;
  const auto& mu = tuning().mutiny;
  say(hero_.charmees.size() == 1 ? "You hide behind your charmee" : "You hide behind your charmees");
  for (size_t i = 0; i < hero_.charmees.size();) {
    Monster& ch = hero_.charmees[i];
    const double mutiny = std::max(mu.baseChance + (mu.perIntOverPlayerInt * (ch.stat(Stat::INT) - hero_.stat(Stat::INT)) +
                                                     mu.perIntOverPlayerCha * (ch.stat(Stat::INT) - hero_.stat(Stat::CHA))),
                                   mu.minChance);
    if (mutiny > rng_.real()) {
      const std::string name = ch.displayName;
      cue(Sfx::CharmFailed, static_cast<int>(ch.cls));
      say("Your " + name + " turns on you and attacks!");
      const double dmg = std::floor((static_cast<double>(rng_.range(1, c.npcDamageDie)) + ch.stat(Stat::STR)) / c.npcDamageStrDivisor);
      // the traitor's blow is a monster's blow: your armor and VIT count, and it never takes more than mutiny.maxDamagePercent of your max HP
      int hurt = scaled(mitigate(dmg, hero_.ac * d.playerAcWhenMonsterHits + hero_.stat(Stat::VIT) * d.playerVitWhenMonsterHits), monsterDamageFactor());
      if (mu.maxDamagePercent > 0) hurt = std::min(hurt, std::max(1, static_cast<int>(std::ceil(hero_.maxHP * mu.maxDamagePercent / 100.0))));
      hero_.hp -= hurt;
      say("Your " + name + " hits you for " + std::to_string(hurt) + " damage");
      say("...and flees!");
      hero_.charmees.erase(hero_.charmees.begin() + static_cast<std::ptrdiff_t>(i));
      if (hero_.hp <= 0) {  // the traitor's blow was the last: no other charmee gets to win the fight afterwards
        say("You have died!");
        outcome_ = CombatOutcome::Died;
        return;
      }
      continue;
    }
    const double roll = (static_cast<double>(rng_.range(1, c.attackRollDie)) + ch.stat(Stat::DEX)) * c.charmeeDexMultiplier;
    if (roll >= monster_.ac) {
      double dmg = std::floor((static_cast<double>(rng_.range(1, c.npcDamageDie)) + ch.stat(Stat::STR)) / c.npcDamageStrDivisor);
      const int dealt = scaled(std::floor(dmg - monster_.ac * d.monsterAcWhenCharmeeHits - monster_.stat(Stat::VIT) * d.monsterVitWhenCharmeeHits), c.playerDamageMultiplier);
      monster_.hp -= dealt;
      cue(Sfx::MonsterAttack, static_cast<int>(ch.cls), 200);
      say("Your " + ch.displayName + " deals: " + std::to_string(dealt));
      if (monster_.hp < 1) {
        killed();
        return;
      }
    } else {
      cue(Sfx::AttackMiss, -1, 200);
      say("Your " + ch.displayName + " attacked but missed!");
    }
    ++i;
  }
}

void Combat::trick() {
  say("You try to outsmart the " + monster_.displayName);
  const double c = trickChance(hero_, monster_);
  if (c > rng_.real()) {
    say("Your trick worked!");
    monster_.hp = 0;
    killed();
  } else {
    say("Trick failed! ");
  }
}

void Combat::pray() {
  say("You desperately pray for assistance...");
  const double c = prayChance(hero_, monster_);
  if (c > rng_.real()) {
    cue(Sfx::Prayer);
    say("A divine light pierces through the shadows!");
    monster_.hp = 0;
    outcome_ = CombatOutcome::Prayed;
  } else {
    say("Your sacred words echo in the void...unheard...");
  }
}

void Combat::monsterTurn(bool playerHiding) {
  const auto& f = tuning().flee;
  const auto& w = tuning().wake;
  if (playerHiding && hero_.charmees.empty()) {
    say("You are no longer hiding!");
    playerHiding = false;
  }
  if (monster_.has(StatusFlag::SLEEPING)) {
    double wake = w.baseChance + w.perIntDelta * (monster_.stat(Stat::INT) - hero_.stat(Stat::INT)) + w.perLevelDelta * (monster_.level - hero_.level);
    if (monster_.has(StatusFlag::UNWAKABLE)) {
      wake = 0;
      monster_.clear(StatusFlag::UNWAKABLE);
    }
    wake = chance(wake, false);
    if (wake > rng_.real()) {
      monster_.clear(StatusFlag::SLEEPING);
      say(monster_.displayName + " wakes up!");
    } else {
      say(monster_.displayName + " is sleeping and cannot attack!");
      return;
    }
  }
  if (monster_.hp < static_cast<double>(monster_.maxHP) / f.considerBelowHpDivisor) {
    if (rng_.real() < f.considerBaseChance + static_cast<double>(hero_.hp - monster_.hp) / f.considerHpGapDivisor) {
      say(monster_.displayName + " is trying to flee!");
      monsterTryFlee();
      return;
    }
  }
  // Specials need a clear shot at you: hiding behind a charmee forces the plain attack.
  const MonsterAction act = playerHiding ? MonsterAction::Attack : chooseAction();
  if (act == MonsterAction::Attack) {
    say(monster_.displayName + " attacks...");
    monsterPlainAttack(playerHiding);
  } else {
    monsterSpecial(act);
  }
  hero_.removeDeadCharmees();
}

void Combat::monsterTryFlee() {
  const auto& f = tuning().flee;
  const double c = chance(f.baseChance + f.perDexDelta * (monster_.stat(Stat::DEX) - hero_.stat(Stat::DEX)) + f.perLevelDelta * (monster_.level - hero_.level), false);
  if (!(rng_.real() <= c)) {
    cue(Sfx::FleeFail);
    say(monster_.displayName + " tries to run, but fails!");
    return;
  }
  cue(Sfx::FleeSuccess);
  say(monster_.displayName + " ran away!");
  monster_.fled = true;
  outcome_ = CombatOutcome::Fled;
}

void Combat::monsterPlainAttack(bool playerHiding) {
  const auto& c = tuning().combat;
  const auto& d = tuning().defense;
  const int targets = 1 + static_cast<int>(hero_.charmees.size());
  const int target = static_cast<int>(rng_.range(playerHiding ? 2 : 1, targets));
  if (target == 1) {
    if (monsterHitsPlayer(0)) {
      const double dmg = monsterRawDamage();
      const int hurt = capHit(scaled(mitigate(dmg, hero_.ac * d.playerAcWhenMonsterHits + hero_.stat(Stat::VIT) * d.playerVitWhenMonsterHits), monsterDamageFactor()));
      hurtPlayer(hurt);
      cue(Sfx::MonsterAttack, static_cast<int>(monster_.cls));
      cue(Sfx::PlayerHit, hurt <= hero_.maxHP * 0.15 ? 0 : hurt <= hero_.maxHP * 0.5 ? 1 : 2, 120);
      say("-->You're hit: " + std::to_string(hurt) + " damage!");
      if (monster_.onHit & static_cast<unsigned>(Ailment::Poison)) afflict(Ailment::Poison, afflictChance(Ailment::Poison, tuning().monsterActions.prefixStatusChance));
      if (monster_.onHit & static_cast<unsigned>(Ailment::Sleep)) afflict(Ailment::Sleep, afflictChance(Ailment::Sleep, tuning().monsterActions.prefixStatusChance));
      if (monster_.onHit & static_cast<unsigned>(Ailment::Blind)) afflict(Ailment::Blind, afflictChance(Ailment::Blind, tuning().monsterActions.prefixStatusChance));
    } else {
      cue(Sfx::AttackMiss);
      say("-->" + monster_.displayName + " missed you.");
    }
    return;
  }
  Monster& ch = hero_.charmees[static_cast<size_t>(target) - 2];
  const double roll = (static_cast<double>(rng_.range(1, c.attackRollDie)) + monster_.stat(Stat::DEX)) * c.monsterDexMultiplier;
  if (roll >= ch.ac) {
    double dmg = std::floor((static_cast<double>(rng_.range(1, c.npcDamageDie)) + monster_.stat(Stat::STR)) / c.npcDamageStrDivisor);
    const int hurt = scaled(mitigate(dmg, ch.ac * d.charmeeAcWhenMonsterHits + ch.stat(Stat::VIT) * d.charmeeVitWhenMonsterHits), monsterDamageFactor());
    ch.hp -= hurt;
    cue(Sfx::MonsterAttack, static_cast<int>(monster_.cls));
    say("-->Your " + ch.displayName + " takes: " + std::to_string(hurt));
    if (ch.hp < 1) {
      cue(Sfx::MonsterDies, static_cast<int>(ch.cls), 300);
      say("Your " + ch.displayName + " died!");
    }
  } else {
    cue(Sfx::AttackMiss);
    say("-->" + ch.displayName + " Evades!");
  }
}

// --- Monster special actions and ailments (data/monsters.json "actions"; numbers in gameplay.json "monsterActions") ---

MonsterAction Combat::chooseAction() const {
  const auto& list = MonsterData::get().actions[static_cast<size_t>(monster_.cls)];
  const double boss = monster_.isBoss ? tuning().monsterActions.bossSpecialMultiplier : 1.0;
  const auto weight = [&](const ActionWeight& w) { return w.weight * (w.action == MonsterAction::Attack ? 1.0 : boss); };
  double total = 0;
  for (const ActionWeight& w : list) total += weight(w);
  double roll = rng_.real() * total;
  for (const ActionWeight& w : list) {
    roll -= weight(w);
    if (roll < 0) return w.action;
  }
  return MonsterAction::Attack;
}

bool Combat::monsterHitsPlayer(double bonus) {
  const auto& c = tuning().combat;
  double hit = c.baseToHit + c.toHitDexAcModifier * (monster_.stat(Stat::DEX) - hero_.ac) + c.toHitLevelModifier * (monster_.level - hero_.level) +
               c.toHitDexDeltaModifier * (monster_.stat(Stat::DEX) - hero_.stat(Stat::DEX)) + bonus;
  hit += static_cast<double>(rng_.range(-c.hitRandomPercent, c.hitRandomPercent)) / 100;
  hit = chance(hit, true);
  return rng_.real() <= hit;
}

double Combat::monsterRawDamage() {
  const auto& c = tuning().combat;
  return std::floor((static_cast<double>(rng_.range(1, c.npcDamageDie)) + monster_.stat(Stat::STR)) / c.npcDamageStrDivisor);
}

void Combat::hurtPlayer(int amount) {
  hero_.hp -= amount;
  if (ail_.sleep > 0 && amount > 0) {
    ail_.sleep = 0;
    say("The blow wakes you!");
  }
}

double Combat::afflictChance(Ailment a, double base) const {
  const auto& m = tuning().monsterActions;
  const Stat s = a == Ailment::Poison ? Stat::VIT : a == Ailment::Sleep ? Stat::INT : Stat::DEX;
  return chance(base + m.prefixPerStatDelta * (monster_.stat(s) - hero_.stat(s)) + m.prefixPerLevelDelta * (monster_.level - hero_.level), true);
}

void Combat::afflict(Ailment a, double c) {
  const auto& m = tuning().monsterActions;
  if (!(rng_.real() <= c)) return;
  switch (a) {
    case Ailment::Poison:
      say(ail_.poison > 0 ? "The poison burns deeper!" : "You are poisoned!");
      ail_.poison = m.poisonTurns;
      say("-->Poisoned: you lose HP each turn for " + std::to_string(m.poisonTurns) + " turns.");
      break;
    case Ailment::Sleep:
      if (ail_.sleep > 0) return;
      say("You fall asleep!");
      ail_.sleep = m.sleepTurns;
      say("-->Asleep: you lose " + std::to_string(m.sleepTurns) + " actions (a hit wakes you).");
      break;
    case Ailment::Blind:
      say(ail_.blind > 0 ? "Your eyes sting more!" : "You are blinded!");
      ail_.blind = m.blindTurns;
      say("-->Blinded: to-hit -" + std::to_string(static_cast<int>(std::lround(m.blindToHitPenalty * 100))) + "% for " + std::to_string(m.blindTurns) + " turns.");
      break;
  }
}

void Combat::tickAilments() {
  const auto& m = tuning().monsterActions;
  if (ail_.poison > 0) {
    const int dmg = std::max(1, static_cast<int>(std::floor(m.poisonDamageBase + m.poisonDamagePerLevel * monster_.level)));
    hero_.hp -= dmg;
    say("-->Poison burns you: " + std::to_string(dmg) + " damage");
    if (--ail_.poison == 0) say("The poison wears off.");
  }
  if (ail_.blind > 0 && --ail_.blind == 0) say("Your vision clears.");
}

void Combat::monsterSpecial(MonsterAction a) {
  const auto& d = tuning().defense;
  const auto& m = tuning().monsterActions;
  const std::string& name = monster_.displayName;
  special_ = {a, special_.seq + 1};
  cue(Sfx::MonsterSpecial, static_cast<int>(a));
  switch (a) {
    case MonsterAction::Drain: {
      say(name + " reaches for your life force...");
      if (!monsterHitsPlayer(0)) {
        cue(Sfx::AttackMiss);
        say("-->" + name + " missed you.");
        break;
      }
      const double raw = monsterRawDamage() * m.drainDamageFactor;
      const int hurt = capHit(scaled(mitigate(raw, hero_.ac * d.playerAcWhenMonsterHits + hero_.stat(Stat::VIT) * d.playerVitWhenMonsterHits), monsterDamageFactor()));
      hurtPlayer(hurt);
      monster_.hp = std::min(monster_.maxHP, monster_.hp + hurt);
      cue(Sfx::MonsterAttack, static_cast<int>(monster_.cls));
      cue(Sfx::PlayerHit, hurt <= hero_.maxHP * 0.15 ? 0 : hurt <= hero_.maxHP * 0.5 ? 1 : 2, 120);
      say("-->It drains " + std::to_string(hurt) + " life from you!");
      break;
    }
    case MonsterAction::Bite: {
      say(name + " bites at you...");
      if (!monsterHitsPlayer(0)) {
        cue(Sfx::AttackMiss);
        say("-->" + name + " missed you.");
        break;
      }
      const int hurt = capHit(scaled(mitigate(monsterRawDamage(), hero_.ac * d.playerAcWhenMonsterHits + hero_.stat(Stat::VIT) * d.playerVitWhenMonsterHits), monsterDamageFactor()));
      hurtPlayer(hurt);
      cue(Sfx::MonsterAttack, static_cast<int>(monster_.cls));
      cue(Sfx::PlayerHit, hurt <= hero_.maxHP * 0.15 ? 0 : hurt <= hero_.maxHP * 0.5 ? 1 : 2, 120);
      say("-->You're bitten: " + std::to_string(hurt) + " damage!");
      afflict(Ailment::Poison, afflictChance(Ailment::Poison, m.biteBleedChance));
      break;
    }
    case MonsterAction::Fire: {
      say(name + " hurls fire at you...");
      if (!monsterHitsPlayer(m.fireToHitBonus)) {
        cue(Sfx::AttackMiss);
        say("-->The flames miss you.");
        break;
      }
      const int hurt = capHit(scaled(mitigate(monsterRawDamage() * m.fireDamageFactor, hero_.stat(Stat::VIT) * d.playerVitWhenMonsterHits), monsterDamageFactor()));
      hurtPlayer(hurt);
      cue(Sfx::MonsterAttack, static_cast<int>(monster_.cls));
      cue(Sfx::PlayerHit, hurt <= hero_.maxHP * 0.15 ? 0 : hurt <= hero_.maxHP * 0.5 ? 1 : 2, 120);
      say("-->You're scorched: " + std::to_string(hurt) + " damage!");
      break;
    }
    case MonsterAction::Lull: {
      say(name + " sings a drowsy lullaby...");
      const double ch = chance(m.lullBaseChance + m.lullPerIntDelta * (monster_.stat(Stat::INT) - hero_.stat(Stat::INT)) + m.lullPerLevelDelta * (monster_.level - hero_.level), true);
      if (ail_.sleep == 0 && rng_.real() <= ch) afflict(Ailment::Sleep, 1.0);
      else say("-->You shake off the song.");
      break;
    }
    case MonsterAction::Dust: {
      say(name + " flings sand at your eyes...");
      const double ch = chance(m.dustBaseChance + m.dustPerDexDelta * (monster_.stat(Stat::DEX) - hero_.stat(Stat::DEX)) + m.dustPerLevelDelta * (monster_.level - hero_.level), true);
      if (rng_.real() <= ch) afflict(Ailment::Blind, 1.0);
      else say("-->You turn away in time.");
      break;
    }
    case MonsterAction::Attack: break;
  }
}

}  // namespace laby
