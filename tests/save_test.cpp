// Save / Restore, the ghost of a dead hero, and the top 10. Everything happens in a temporary state folder.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "game/Ghost.hpp"
#include "game/Persist.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;
namespace fs = std::filesystem;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

static std::string slurp(const fs::path& p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void spit(const fs::path& p, const std::string& text) { std::ofstream(p) << text; }

// From the title: pick the first row (Continue when there is a save, else a new run).
static void pressEnter(WalkSession& s) { s.handle(Key::Enter); }

static int saveRoundTrip(const fs::path& dir) {
  fs::remove_all(dir);
  WalkSession a(11);
  a.setStateDir(dir.string());
  a.debugGiveKit();
  a.debugGoToTown();
  CHECK(a.inTown());
  CHECK(!a.hasSave());
  const size_t scrollsBefore = a.hero().scrolls.size();  // (the random starting item may be a scroll)
  // the sign post: Save and Exit is the fifth row
  for (int i = 0; i < 4; ++i) a.handle(Key::Down);
  a.handle(Key::Enter);
  CHECK(a.onTitle());
  CHECK(a.hasSave());
  CHECK(fs::exists(dir / "save.json"));
  const std::string first = slurp(dir / "save.json");
  fs::copy_file(dir / "save.json", dir / "first.json", fs::copy_options::overwrite_existing);

  // a fresh process: Continue is the first row, loads, and uses the save up
  WalkSession b(99);
  b.setStateDir(dir.string());
  b.startTitle();
  CHECK(b.hasSave());
  pressEnter(b);
  CHECK(b.inGame() && b.inTown());
  CHECK(!b.hasSave());
  CHECK(!fs::exists(dir / "save.json"));
  CHECK(b.hero().scrolls.size() == scrollsBefore);
  CHECK(b.hero().charmees.size() == 2);
  CHECK(b.hero().gold == 500);

  // saving it again gives the identical file: nothing was lost or changed on the way
  for (int i = 0; i < 4; ++i) b.handle(Key::Down);
  b.handle(Key::Enter);
  CHECK(slurp(dir / "save.json") == first);

  // the dungeon after a restore plays like it would have without the save (same random numbers)
  WalkSession c(99);
  c.setStateDir(dir.string());
  c.startTitle();
  pressEnter(c);
  WalkSession d(11);
  d.debugGiveKit();
  d.debugGoToTown();
  for (WalkSession* s : {&c, &d}) {
    s->handle(Key::Down), s->handle(Key::Down), s->handle(Key::Down);
    s->handle(Key::Enter);  // The Labyrinth
    CHECK(s->inElevator());
    s->handle(Key::Down);
    s->handle(Key::Down);  // floor 2: built fresh in both (a restored game has no floor 1 built either)
    s->handle(Key::Enter);
    CHECK(!s->inElevator());
  }
  CHECK(c.floor() == 2 && d.floor() == 2);
  CHECK(c.player().x == d.player().x && c.player().y == d.player().y);
  return 0;
}

static int badFiles(const fs::path& dir) {
  fs::remove_all(dir);
  fs::create_directories(dir);
  for (const char* junk : {"", "not json", "{\"version\":99}", "{\"version\":1}", "{\"version\":1,\"seed\":\"x\",\"hero\":{}}"}) {
    spit(dir / "save.json", junk);
    SaveData d;
    CHECK(loadSave(dir.string(), d) == LoadResult::Corrupt);
    CHECK(!fs::exists(dir / "save.json"));
    CHECK(fs::exists(dir / "save.corrupt.json"));
    fs::remove(dir / "save.corrupt.json");
  }
  SaveData d;
  CHECK(loadSave(dir.string(), d) == LoadResult::Missing);
  // a truncated real save
  WalkSession a(5);
  a.setStateDir(dir.string());
  a.debugGoToTown();
  for (int i = 0; i < 4; ++i) a.handle(Key::Down);
  a.handle(Key::Enter);
  const std::string text = slurp(dir / "save.json");
  spit(dir / "save.json", text.substr(0, text.size() / 2));
  CHECK(loadSave(dir.string(), d) == LoadResult::Corrupt);
  return 0;
}

static Item gear(EquipmentType t, int level, int ac, int bonus) {
  Item i;
  i.type = t;
  i.name = i.displayName = "Thing";
  i.level = level;
  i.ac = ac;
  i.bonuses = {bonus, 0, 0, 0, 0};
  return i;
}

static int ghostRules() {
  Rng rng(3);
  Hero h;
  h.name = "Ada";
  h.level = 7;
  h.experience = 100;
  h.gold = 321;
  h.base = {9, 10, 11, 12, 13};
  h.maxHP = 55;
  h.ac = 6;
  h.equipment[0] = gear(EquipmentType::Weapon, 3, 0, 2);   // 5
  h.equipment[1] = gear(EquipmentType::ChestArmor, 4, 3, 1);    // 8  <- best
  h.equipment[2] = gear(EquipmentType::Helmet, 4, 3, 1);   // 8, a tie: the first one stays
  CHECK(Ghost::rate(*h.equipment[0]) == 5);
  const Ghost g = Ghost::fromHero(h, 4, rng);
  CHECK(g.loot.type == EquipmentType::ChestArmor);
  CHECK(g.experience == 26);  // ceil(101 / 4)
  CHECK(g.floor == 4 && g.gold == 321 && g.level == 7);
  const Monster m = g.monster();
  CHECK(m.displayName == "Spectral Ada");
  CHECK(m.cls == MonsterClass::UNDEAD);
  CHECK(m.hp == m.maxHP && m.maxHP == 55);
  CHECK(m.stats[0] == 9 && m.stats[4] == 13);  // the base stats, no gear
  CHECK(g.treasure().gold == 321 && g.treasure().item.type == EquipmentType::ChestArmor);

  Hero naked;
  naked.level = 5;
  const Ghost n = Ghost::fromHero(naked, 2, rng);  // nothing worn: a new piece of equipment, level + 2
  CHECK(n.loot.isEquipment());
  CHECK(n.experience == 1);
  return 0;
}

static int scoresTable() {
  std::vector<ScoreEntry> list;
  CHECK(insertScore(list, {"zero", 0, 1, 1, 1}) == 0);
  CHECK(insertScore(list, {"a", 100, 1, 1, 1}) == 1);
  CHECK(insertScore(list, {"b", 50, 1, 1, 1}) == 2);
  CHECK(insertScore(list, {"c", 100, 1, 1, 1}) == 2);  // a tie goes below the older one
  CHECK(list[0].name == "a" && list[1].name == "c" && list[2].name == "b");
  for (int i = 0; i < 20; ++i) insertScore(list, {"x", 10 + i, 1, 1, 1});
  CHECK(list.size() == kLegends);
  CHECK(insertScore(list, {"low", 1, 1, 1, 1}) == 0);
  CHECK(list.back().score >= 20);

  const fs::path dir = fs::temp_directory_path() / "labyterm_scores_test";
  fs::remove_all(dir);
  CHECK(loadScores(dir.string()).empty());
  CHECK(recordScore(dir.string(), {"hi", 500, 3, 4, 5}) == 1);
  CHECK(recordScore(dir.string(), {"lo", 100, 3, 4, 5}) == 2);
  const auto back = loadScores(dir.string());
  CHECK(back.size() == 2 && back[0].name == "hi" && back[0].floor == 3 && back[1].steps == 5);
  spit(dir / "scores.json", "garbage");
  CHECK(loadScores(dir.string()).empty());
  return 0;
}

static int deathAndGhost(const fs::path& dir) {
  fs::remove_all(dir);
  WalkSession a(21);
  a.setStateDir(dir.string());
  a.debugGiveKit();
  a.debugEndRun();
  CHECK(a.gameOver());
  CHECK(fs::exists(dir / "graveyard.json"));
  CHECK(fs::exists(dir / "scores.json"));
  CHECK(a.summary() && a.summary()->rank == 1);
  CHECK(!fs::exists(dir / "save.json"));  // dying leaves no save

  // the next run: through the title and the creation screen; the ghost is taken out of the graveyard
  WalkSession b(22);
  b.setStateDir(dir.string());
  b.startTitle();
  pressEnter(b);          // Enter the Labyrinth -> creation
  pressEnter(b);          // (the cursor is on "Enter the Labyrinth!")
  CHECK(b.inGame());
  CHECK(b.ghost().has_value());
  CHECK(!fs::exists(dir / "graveyard.json"));  // one chance
  const int floor = b.ghost()->floor;
  CHECK(floor == 1);
  b.debugFight();  // chance 1 (see main): the ghost on its floor
  CHECK(b.fightMonster() && b.fightMonster()->displayName.rfind("Spectral ", 0) == 0);
  CHECK(b.fightMonster()->cls == MonsterClass::UNDEAD);
  CHECK(!b.ghost());  // it comes once
  return 0;
}

// A save from before the RNG state became text has "rng" as four numbers: it must still load (with a fresh sequence from the seed).
static int oldRngFormat(const fs::path& dir) {
  fs::remove_all(dir);
  WalkSession a(11);
  a.setStateDir(dir.string());
  a.debugGoToTown();
  for (int i = 0; i < 4; ++i) a.handle(Key::Down);
  a.handle(Key::Enter);
  CHECK(fs::exists(dir / "save.json"));
  std::string text = slurp(dir / "save.json");
  const size_t at = text.find("\"rng\"");
  CHECK(at != std::string::npos);
  const size_t q1 = text.find('"', at + 5), q2 = text.find('"', q1 + 1);  // the string value's quotes
  CHECK(q1 != std::string::npos && q2 != std::string::npos);
  text.replace(q1, q2 - q1 + 1, "[1,2,3,4]");
  spit(dir / "save.json", text);
  WalkSession b(99);
  b.setStateDir(dir.string());
  b.startTitle();
  CHECK(b.hasSave());
  pressEnter(b);
  CHECK(b.inGame() && b.inTown());
  return 0;
}

int main() {
  // every encounter is the ghost, so the test need not wait for a 25 % roll
  const fs::path tuning = fs::temp_directory_path() / "labyterm_save_test_tuning.json";
  spit(tuning, "{\"encounters\":{\"corpseChance\":1.0}}");
  setenv("LABYTERM_TUNING", tuning.c_str(), 1);

  const fs::path dir = fs::temp_directory_path() / "labyterm_save_test";
  if (int r = saveRoundTrip(dir)) return r;
  if (int r = oldRngFormat(dir)) return r;
  if (int r = badFiles(dir)) return r;
  if (int r = ghostRules()) return r;
  if (int r = scoresTable()) return r;
  if (int r = deathAndGhost(dir)) return r;
  fs::remove_all(dir);
  std::puts("save_test: all passed");
  return 0;
}
