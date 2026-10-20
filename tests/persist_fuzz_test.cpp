// Hostile state files: a real save, ghost and scores table are truncated and mutated field by field (deleted, wrong type, negative,
// huge, NaN-ish), and every load must come back Ok or Corrupt without crashing; saves that load are then played and drawn.
// The point is to run this under the sanitizers (tools/sanitize.sh): a plain run only proves nothing crashes.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "game/Ghost.hpp"
#include "game/Persist.hpp"
#include "platform/Theme.hpp"
#include "render/CellGrid.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;
namespace fs = std::filesystem;
using nlohmann::json;

#define CHECK(cond)                                           \
  do {                                                        \
    if (!(cond)) {                                            \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__); \
      return 1;                                               \
    }                                                         \
  } while (0)

static std::string slurp(const fs::path& p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
static void spit(const fs::path& p, const std::string& text) { std::ofstream(p) << text; }

// Every replacement value tried on every leaf (plus deleting the leaf).
static std::vector<json> hostileValues() {
  return {json(nullptr), json("x"), json(-1), json(0), json(true), json::array(), json::object(),
          json(4294967296LL), json(1e30), json(-1e30), json(2147483648.0)};
}

// Calls fn(text) for the document with each leaf deleted or replaced.
template <class Fn>
static void mutate(const json& doc, Fn fn) {
  const json flat = doc.flatten();
  for (auto it = flat.begin(); it != flat.end(); ++it) {
    if (it.key() == "/version") continue;  // covered by versionFields()
    const json::json_pointer ptr(it.key());
    for (const json& v : hostileValues()) {
      json m = doc;
      m[ptr] = v;
      fn(m.dump());
    }
    json m = doc;  // delete the leaf
    json& parent = m.at(ptr.parent_pointer());
    if (parent.is_object()) parent.erase(ptr.back());
    else if (parent.is_array()) parent.erase(static_cast<size_t>(std::stoul(ptr.back())));
    fn(m.dump());
  }
}

static fs::path gDir;
static long gOk = 0, gCorrupt = 0, gOddHero = 0;

// Loads `text` as save.json, and when it is accepted, continues from the title and plays on.
static int tryDocument(const std::string& text, bool playThrough) {
  fs::remove(gDir / "save.corrupt.json");
  spit(gDir / "save.json", text);
  if (playThrough) {
    WalkSession s(1);
    s.setStateDir(gDir.string());
    s.startTitle();
    CHECK(s.hasSave());
    s.handle(Key::Enter);  // Continue: loads, or drops the corrupt file and starts a fresh run
    CHECK(!fs::exists(gDir / "save.json"));
    static Theme theme;
    static CellGrid grid(152, 40);
    static const Key keys[] = {Key::Down, Key::Enter, Key::Up, Key::Enter, Key::Left, Key::Right, Key::Enter, Key::Down, Key::Down, Key::Enter};
    for (int i = 0; i < 40; ++i) {
      s.handle(keys[i % 10]);
      if (i % 5 == 0) s.draw(grid, theme, "fuzz");
    }
    return 0;
  }
  SaveData d;
  const LoadResult r = loadSave(gDir.string(), d);
  CHECK(!fs::exists(gDir / "save.json"));
  if (r == LoadResult::Ok) {
    ++gOk;
    if (d.hero.hp > d.hero.maxHP || d.hero.hp < 0 || d.hero.level < 1 || d.hero.gold < 0) ++gOddHero;
    if (d.ghost) {
      (void)d.ghost->monster();
      (void)d.ghost->treasure();
    }
  } else {
    CHECK(r == LoadResult::Corrupt);
    ++gCorrupt;
  }
  return 0;
}

// A "version" that is there but is not a number must count as an unusable file, not escape as an exception.
static int versionFields() {
  int bad = 0;
  for (const char* name : {"save.json", "graveyard.json", "scores.json"})
    for (const char* v : {"null", "\"x\"", "[]", "{}", "true"}) {
      spit(gDir / name, std::string("{\"version\":") + v + "}");
      try {
        SaveData d;
        if (std::string(name) == "save.json") (void)loadSave(gDir.string(), d);
        else if (std::string(name) == "graveyard.json") (void)takeGhost(gDir.string());
        else (void)loadScores(gDir.string());
      } catch (const std::exception& e) {
        std::printf("UNCAUGHT in %s with \"version\": %s -> %s\n", name, v, e.what());
        ++bad;
      }
      fs::remove(gDir / name);
      fs::remove(gDir / "save.corrupt.json");
    }
  return bad;
}

int main() {
  const fs::path tuning = fs::temp_directory_path() / "labyterm_fuzz_tuning.json";
  spit(tuning, "{\"encounters\":{\"corpseChance\":1.0}}");  // a loaded ghost is met at once
  setenv("LABYTERM_TUNING", tuning.c_str(), 1);
  gDir = fs::temp_directory_path() / "labyterm_persist_fuzz";
  fs::remove_all(gDir);
  fs::create_directories(gDir);

  // A real save with a gear-heavy hero and a ghost waiting on the way down.
  {
    WalkSession a(21);
    a.setStateDir(gDir.string());
    a.debugGiveKit();
    a.debugGoToTown();
    for (int i = 0; i < 4; ++i) a.handle(Key::Down);
    a.handle(Key::Enter);
  }
  const std::string real = slurp(gDir / "save.json");
  CHECK(!real.empty());
  json doc = json::parse(real);
  // make sure the optional parts exist so they get mutated too
  if (doc["ghost"].is_null()) {
    Ghost g;
    g.name = "Ada";
    g.level = 3;
    g.maxHP = 20;
    g.loot.name = g.loot.displayName = "Sword";
    SaveData tmp;
    Hero h;
    h.name = "Ada";
    tmp.hero = h;
    tmp.ghost = g;
    doc["ghost"] = json::parse((writeSave(gDir.string(), tmp), slurp(gDir / "save.json")))["ghost"];
  }

  // 1. truncations of the real save, at many lengths
  for (size_t n = 0; n < real.size(); n += 13) CHECK(tryDocument(real.substr(0, n), false) == 0);
  // 2. every leaf mutated, loaded directly
  size_t docs = 0;
  mutate(doc, [&](const std::string& text) {
    ++docs;
    if (tryDocument(text, false) != 0) std::exit(1);
  });
  // 3. the mutations that still load, played through a WalkSession (a sample, to keep the run short)
  size_t played = 0;
  mutate(doc, [&](const std::string& text) {
    SaveData probe;
    spit(gDir / "save.json", text);
    if (loadSave(gDir.string(), probe) != LoadResult::Ok || (played++ % 40) != 0) return;
    if (tryDocument(text, true) != 0) std::exit(1);
  });
  std::printf("save: %zu mutated documents, %ld loaded, %ld corrupt, %ld loaded with odd hero numbers (hp>max, negative, level<1), %zu played\n",
              docs, gOk, gCorrupt, gOddHero, (played + 39) / 40);

  // ghost and scores files
  if (!doc["ghost"].is_null()) {
    const json gdoc = {{"version", kSaveVersion}, {"ghost", doc["ghost"]}};
    mutate(gdoc, [&](const std::string& text) {
      spit(gDir / "graveyard.json", text);
      if (auto g = takeGhost(gDir.string())) {
        (void)g->monster();
        (void)g->treasure();
      }
      if (fs::exists(gDir / "graveyard.json")) std::exit(1);
    });
  }
  const json sdoc = {{"version", kSaveVersion},
                     {"entries", json::array({{{"name", "A"}, {"score", 5}, {"floor", 1}, {"level", 1}, {"steps", 9}},
                                              {{"name", "B"}, {"score", 3}, {"floor", 2}, {"level", 2}, {"steps", 8}}})}};
  mutate(sdoc, [&](const std::string& text) {
    spit(gDir / "scores.json", text);
    if (loadScores(gDir.string()).size() > kLegends) std::exit(1);
    ScoreEntry e;
    e.name = "Z";
    e.score = 4;
    (void)recordScore(gDir.string(), e);  // also rewrites over whatever was there
  });

  const int badVersions = versionFields();
  fs::remove_all(gDir);
  if (badVersions) {
    std::printf("FAILED: %d non-numeric \"version\" cases escaped as exceptions\n", badVersions);
    return 1;
  }
  std::puts("persist_fuzz_test: all passed");
  return 0;
}
