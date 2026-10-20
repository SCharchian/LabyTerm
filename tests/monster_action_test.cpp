// Monster special actions and prefix status effects (Combat::monsterSpecial, Ailments): every class uses its own special,
// the ailments apply, tick, wear off and can be countered, and the prefix data says what it should.
#include <cstdio>
#include <string>
#include <vector>

#include "core/Tuning.hpp"
#include "game/Combat.hpp"

using namespace laby;

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); ++failures; } } while (0)

static Hero strongHero(Rng& rng) {
  Hero h;
  h.roll(rng);
  h.base = {12, 12, 12, 12, 12};
  h.recalc();
  h.maxHP = h.hp = 5000;  // survives anything: these tests watch the effects, not the outcome
  return h;
}

static Monster hostile(MonsterClass cls, int level) {
  Rng r(7);
  Monster m = Monster::create(r, static_cast<int>(cls) + 1, level, false);
  m.cls = cls;
  m.onHit = 0;
  m.stats = {40, 40, 40, 40, 10};  // hits often, resists nothing of the hero's
  m.maxHP = m.hp = 100000;
  return m;
}

// Runs fights of `turns` turns for several seeds; `look` is called after every turn with the log of that turn.
template <class F>
static void run(MonsterClass cls, unsigned onHit, int turns, F look) {
  for (int seed = 1; seed <= 60; ++seed) {
    Rng rng(seed);
    Hero h = strongHero(rng);
    Monster m = hostile(cls, 5);
    m.onHit = onHit;
    std::vector<std::string> log;
    Combat c(h, m, rng, [&](const std::string& s) { log.push_back(s); });
    for (int t = 0; t < turns && c.outcome() == CombatOutcome::Ongoing; ++t) {
      log.clear();
      const int mhp = c.monster().hp;
      c.act(CombatAction::Attack);
      look(c, h, log, mhp);
    }
  }
}

static bool has(const std::vector<std::string>& log, const char* needle) {
  for (const auto& l : log)
    if (l.find(needle) != std::string::npos) return true;
  return false;
}

int main() {
  // data
  const auto& d = MonsterData::get();
  for (int c = 0; c < kMonsterClassCount; ++c) CHECK(!d.actions[static_cast<size_t>(c)].empty(), "every class has actions");
  unsigned poison = 0, blind = 0, sleep = 0;
  for (const auto& p : d.prefixes) {
    poison += (p.status & static_cast<unsigned>(Ailment::Poison)) != 0;
    blind += (p.status & static_cast<unsigned>(Ailment::Blind)) != 0;
    sleep += (p.status & static_cast<unsigned>(Ailment::Sleep)) != 0;
  }
  CHECK(poison && blind && sleep, "the prefixes cover poison, blind and sleep");

  // each class's special shows up
  const struct { MonsterClass cls; const char* msg; } specials[] = {{MonsterClass::UNDEAD, "drains"}, {MonsterClass::ANIMAL, "bitten"}, {MonsterClass::DEMON, "scorched"},
                                                                    {MonsterClass::FAE, "lullaby"}, {MonsterClass::HUMAN, "sand"}};
  for (const auto& s : specials) {
    bool seen = false;
    run(s.cls, 0, 12, [&](const Combat&, const Hero&, const std::vector<std::string>& log, int) { seen |= has(log, s.msg); });
    CHECK(seen, s.msg);
  }

  // drain heals the monster
  {
    bool healed = false;
    for (int seed = 1; seed <= 60 && !healed; ++seed) {
      Rng rng(seed);
      Hero h = strongHero(rng);
      Monster m = hostile(MonsterClass::UNDEAD, 5);
      m.maxHP = 100000;
      m.hp = 99000;
      Combat c(h, m, rng, [&](const std::string&) {});
      for (int t = 0; t < 12 && !healed; ++t) {
        h.recalc();
        const int before = c.monster().hp;
        c.act(CombatAction::Trick);  // a failed trick costs the hero nothing
        if (c.outcome() != CombatOutcome::Ongoing) break;
        healed |= c.monster().hp > before;
      }
    }
    CHECK(healed, "drain heals the monster");
  }

  // prefix poison: applies, burns every turn, wears off
  {
    bool applied = false, burned = false, wore = false;
    run(MonsterClass::HUMAN, static_cast<unsigned>(Ailment::Poison), 14, [&](const Combat& c, const Hero&, const std::vector<std::string>& log, int) {
      applied |= c.ailments().poison > 0;
      burned |= has(log, "Poison burns");
      wore |= has(log, "poison wears off");
      CHECK(c.ailments().poison >= 0 && c.ailments().poison <= tuning().monsterActions.poisonTurns, "poison turns stay in range");
    });
    CHECK(applied && burned && wore, "poison applies, burns and wears off");
  }

  // blind: weakens attacks and fades
  {
    bool blind_ = false, cleared = false, swung = false;
    run(MonsterClass::HUMAN, static_cast<unsigned>(Ailment::Blind), 14, [&](const Combat& c, const Hero&, const std::vector<std::string>& log, int) {
      blind_ |= c.ailments().blind > 0;
      cleared |= has(log, "vision clears");
      swung |= has(log, "swing blindly");
    });
    CHECK(blind_ && cleared && swung, "blindness applies, spoils attacks and fades");
  }

  // sleep: the action is lost (the monster's HP does not change), a hit wakes you
  {
    bool slept = false, lost = false, woken = false;
    run(MonsterClass::FAE, static_cast<unsigned>(Ailment::Sleep), 14, [&](const Combat& c, const Hero&, const std::vector<std::string>& log, int mhp) {
      if (has(log, "fast asleep")) {
        slept = true;
        lost |= c.monster().hp == mhp;
      }
      woken |= has(log, "The blow wakes you") || has(log, "You wake up");
      CHECK(c.ailments().sleep >= 0 && c.ailments().sleep <= tuning().monsterActions.sleepTurns, "sleep turns stay in range");
    });
    CHECK(slept && lost && woken, "sleep skips your action and ends");
  }

  // counterplay: hiding behind a charmee forces the plain attack (no specials)
  {
    bool special = false;
    for (int seed = 1; seed <= 40; ++seed) {
      Rng rng(seed);
      Hero h = strongHero(rng);
      h.charmees.push_back(hostile(MonsterClass::HUMAN, 3));
      h.charmees.back().hp = h.charmees.back().maxHP = 1000000;
      h.charmees.back().stats = {1, 1, 1, 1, 1};
      Monster m = hostile(MonsterClass::UNDEAD, 5);
      std::vector<std::string> log;
      Combat c(h, m, rng, [&](const std::string& s) { log.push_back(s); });
      for (int t = 0; t < 6 && c.outcome() == CombatOutcome::Ongoing && !h.charmees.empty(); ++t) {
        log.clear();
        c.act(CombatAction::Hide);
        if (!h.charmees.empty()) special |= has(log, "life force");  // (a mutiny ends the hiding)
      }
    }
    CHECK(!special, "hiding stops special actions");
  }

  // hiding: a mutinous charmee can kill the hero; a later charmee finishing the monster must not turn that into a win at 0 HP
  {
    int dead = 0;
    for (int seed = 1; seed <= 40; ++seed) {
      Rng rng(seed);
      Hero h = strongHero(rng);
      h.maxHP = h.hp = 1;  // (a mutiny never takes more than mutiny.maxDamagePercent of max HP, but always at least 1)
      Monster traitor = hostile(MonsterClass::HUMAN, 3);  // INT far above the hero's: turns on you for sure, and hits hard
      traitor.stats = {1000, 1000, 1000, 1000, 1000};
      Monster killer = hostile(MonsterClass::HUMAN, 3);  // loyal (floor chance only), always hits, kills what is left
      killer.stats = {1000, 1000, 0, 1000, 0};
      h.charmees = {traitor, killer};
      Monster m = hostile(MonsterClass::UNDEAD, 5);
      m.ac = 0;
      m.maxHP = m.hp = 1;
      Combat c(h, m, rng, [&](const std::string&) {});
      c.act(CombatAction::Hide);
      if (h.hp <= 0) {
        ++dead;
        CHECK(c.outcome() == CombatOutcome::Died, "a hero at 0 HP or less is dead, even if a charmee killed the monster");
      }
    }
    CHECK(dead > 0, "the mutiny scenario happened");
  }

  std::printf(failures ? "monster_action_test: %d failure(s)\n" : "monster_action_test: ok\n", failures);
  return failures ? 1 : 0;
}
