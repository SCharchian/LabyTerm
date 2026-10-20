#pragma once
#include <functional>
#include <string>

#include "audio/Cues.hpp"
#include "core/Rng.hpp"
#include "game/Hero.hpp"
#include "game/Monster.hpp"

namespace laby {

enum class CombatAction { Attack, Charm, Scroll, Hide, Trick, Pray };
inline constexpr int kCombatActionCount = 6;

enum class CombatOutcome {
  Ongoing,
  Won,      // the monster died
  Fled,     // the monster ran away (a boss still leaves its treasure)
  Charmed,  // the monster joined the player's party
  Died,     // the player died
  Prayed    // the prayer was answered: the fight is over and the player leaves the dungeon
};

// What is wrong with the player in this fight (turns left; 0 = fine). Lasts only until the fight ends.
struct Ailments {
  int poison = 0;  // lose HP each turn
  int sleep = 0;   // your actions are skipped; a hit on you wakes you
  int blind = 0;   // your attacks are less likely to hit
};

// One fight (Monster.lua DoCombat and everything it calls): the player picks an action with act(), the monster
// answers if it is still there. All messages go through `say`, in the order the original logged them.
class Combat {
 public:
  using Log = std::function<void(const std::string&)>;
  Combat(Hero& hero, Monster monster, Rng& rng, Log say, int floor = 1);

  // Sounds of the fight (sound effect, its argument, a delay in ms), reported where the original played them. Never touches the game's Rng.
  using Cues = std::function<void(Sfx, int, int)>;
  void setCues(Cues c) { cues_ = std::move(c); }

  const Monster& monster() const { return monster_; }
  const Ailments& ailments() const { return ail_; }
  // The latest special action a monster used (`seq` counts them, 0 = none yet): the game announces it with a banner.
  struct SpecialEvent { MonsterAction action = MonsterAction::Attack; int seq = 0; };
  const SpecialEvent& lastSpecial() const { return special_; }
  CombatOutcome outcome() const { return outcome_; }
  void debugKill() { monster_.hp = 0; killed(); }  // DEBUG: the monster dies on the spot, as if you had killed it
  // False (with the reason in `why`) for actions that cannot be used right now.
  bool available(CombatAction a, std::string* why = nullptr) const;
  // Performs the action and the monster's reply. Unavailable actions only log the reason and cost no turn.
  void act(CombatAction a);
  // Casts a carried scroll at the monster (the Scroll action's second step): MP, the chance it crumbles, the effect, and the monster's reply
  // if the scroll costs a turn. Not enough MP or a scroll you don't have: nothing happens.
  void cast(ScrollType type);
  // The chances of the three "other" actions (also used by the balance bot, so it never copies the formulas):
  // Charm / Trick: base + slope * (CHA or INT over the average hero's of this level, relative) + level delta, clamped; Pray: faith + desperation, capped.
  static double charmChance(const Hero& hero, const Monster& monster);
  static double trickChance(const Hero& hero, const Monster& monster);
  static double prayChance(const Hero& hero, const Monster& monster);
  // The XP a kill (or charm) is worth to this hero: backed off when the hero is levels above the monster.
  static int experienceFor(const Hero& hero, const Monster& monster);
  // True once after ESP was cast: the game shows the monster's numbers.
  bool takeEsp() { const bool e = esp_; esp_ = false; return e; }

 private:
  double chance(double c, bool capTop) const;
  double mitigate(double raw, double defense) const;  // defense.proportional
  int capHit(int hurt) const;          // combat.maxHitPercent / maxHitPercentBoss: no single hit takes more than that share of the hero's max HP
  double monsterDamageFactor() const;  // combat.monsterDamageMultiplier, times combat.bossDamageMultiplier against a boss
  void playerAttack();
  void charm();
  void hide();
  void trick();
  void pray();
  void monsterTurn(bool playerHiding);
  void monsterPlainAttack(bool playerHiding);
  MonsterAction chooseAction() const;
  void monsterSpecial(MonsterAction a);
  bool monsterHitsPlayer(double bonus);   // the to-hit roll against the player
  double monsterRawDamage();              // floor((d6 + STR) / 2), before armor
  void hurtPlayer(int amount);            // damage taken: the hit wakes a sleeper
  void afflict(Ailment a, double chance); // rolls `chance` (resistance already in) and applies the ailment
  double afflictChance(Ailment a, double base) const;
  bool sleepsThrough();                   // asleep: loses the action; true if so
  void tickAilments();                    // poison burns, blindness fades (once per full turn)
  void monsterTryFlee();
  void finishTurn(bool monstersTurn, bool hiding);
  void killed();  // XP, gold, level up
  // The monster's name goes to the log as "\x01[Name]\x02"; the log view draws that span in red.
  void say(const std::string& s) const {
    const std::string& n = monster_.displayName;
    std::string out = s;
    for (size_t at = n.empty() ? std::string::npos : out.find(n); at != std::string::npos; at = out.find(n, at)) {
      const std::string marked = "\x01[" + n + "]\x02";
      out.replace(at, n.size(), marked);
      at += marked.size();
    }
    log_(out);
  }
  void cue(Sfx s, int arg = -1, int delayMs = 0) const { if (cues_) cues_(s, arg, delayMs + (monsterPhase_ ? 500 : 0)); }

  Hero& hero_;
  Monster monster_;
  Rng& rng_;
  Log log_;
  Cues cues_;
  int floor_ = 1;
  bool esp_ = false;
  bool monsterPhase_ = false;  // the monster is answering: its sounds come after the player's
  Ailments ail_;
  SpecialEvent special_;
  CombatOutcome outcome_ = CombatOutcome::Ongoing;
};

}  // namespace laby
