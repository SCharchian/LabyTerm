#include "game/Persist.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace laby {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

std::optional<json> readJson(const fs::path& path) {
  std::ifstream in(path);
  if (!in) return std::nullopt;
  json j = json::parse(in, nullptr, false);
  const auto v = j.is_object() ? j.find("version") : j.end();
  if (j.is_discarded() || !j.is_object() || v == j.end() || !v->is_number_integer() || *v != kSaveVersion) return json(nullptr);  // null: there, but unusable
  return j;
}

bool writeJson(const fs::path& path, const json& j) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return false;
    out << j.dump(1) << "\n";
    out.flush();
    if (!out) return false;
  }
  fs::rename(tmp, path, ec);
  return !ec;
}

// An integer field from a file: read as a double first, so a huge or odd value is refused here instead of overflowing later arithmetic.
constexpr double kSane = 1e6, kSaneBig = 1e8;  // kSaneBig: counters that can legitimately run high (gold, score, steps, experience)
int whole(const json& j, const char* key, double limit = kSane) {
  const double v = j.at(key).get<double>();
  if (!(v >= -limit && v <= limit)) throw std::runtime_error(std::string("out of range: ") + key);
  return static_cast<int>(v);
}

json stats(const StatTable& s) { return json(s); }
StatTable statsOf(const json& j) {
  StatTable s{};
  if (!j.is_array() || j.size() != s.size()) throw std::runtime_error("bad stats");
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = j[i].get<double>();
    if (!(s[i] >= -kSane && s[i] <= kSane)) throw std::runtime_error("bad stat");
  }
  return s;
}

json toJson(const Item& i) {
  return {{"name", i.name}, {"prefix", i.prefix}, {"displayName", i.displayName}, {"type", static_cast<int>(i.type)},
          {"weaponType", static_cast<int>(i.weaponType)}, {"level", i.level}, {"ac", i.ac}, {"hp", i.hp}, {"mp", i.mp},
          {"bonuses", i.bonuses}, {"scroll", static_cast<int>(i.scroll)}, {"desc", i.desc}, {"levelReq", i.levelReq},
          {"intReq", i.intReq}, {"mpCost", i.mpCost}, {"disintegrate", i.disintegrate}, {"costsTurn", i.costsTurn},
          {"combat", i.combat}, {"utility", i.utility}, {"masterwork", i.masterwork}};
}
Item itemOf(const json& j) {
  Item i;
  i.name = j.at("name");
  i.prefix = j.at("prefix");
  i.displayName = j.at("displayName");
  const int type = whole(j, "type"), wt = whole(j, "weaponType"), sc = whole(j, "scroll");
  if (type < 0 || type >= kEquipmentTypeCount || wt < 0 || wt >= kWeaponTypeCount || sc < 0 || sc >= kScrollTypeCount) throw std::runtime_error("bad item");
  i.type = static_cast<EquipmentType>(type);
  i.weaponType = static_cast<WeaponType>(wt);
  i.scroll = static_cast<ScrollType>(sc);
  i.level = whole(j, "level"); i.ac = whole(j, "ac"); i.hp = whole(j, "hp"); i.mp = whole(j, "mp");
  i.bonuses = j.at("bonuses").get<std::array<int, 5>>();
  for (int b : i.bonuses) if (b < -kSane || b > kSane) throw std::runtime_error("bad bonus");
  i.desc = j.at("desc").get<std::array<std::string, 3>>();
  i.levelReq = whole(j, "levelReq"); i.intReq = whole(j, "intReq"); i.mpCost = whole(j, "mpCost");
  i.disintegrate = j.at("disintegrate");
  i.costsTurn = j.at("costsTurn"); i.combat = j.at("combat"); i.utility = j.at("utility");
  i.masterwork = j.value("masterwork", false);
  return i;
}

json toJson(const Monster& m) {
  return {{"displayName", m.displayName}, {"cls", static_cast<int>(m.cls)}, {"image", m.image}, {"stats", stats(m.stats)},
          {"hover", m.hover}, {"level", m.level}, {"ac", m.ac}, {"maxHP", m.maxHP}, {"hp", m.hp}, {"experience", m.experience},
          {"isBoss", m.isBoss}, {"fled", m.fled}, {"charmed", m.charmed}, {"status", m.status}};
}
Monster monsterOf(const json& j) {
  Monster m;
  m.displayName = j.at("displayName");
  const int cls = whole(j, "cls");
  if (cls < 0 || cls >= kMonsterClassCount) throw std::runtime_error("bad monster class");
  m.cls = static_cast<MonsterClass>(cls);
  m.image = j.at("image");
  m.stats = statsOf(j.at("stats"));
  m.hover = j.at("hover");
  m.level = whole(j, "level"); m.ac = whole(j, "ac"); m.maxHP = whole(j, "maxHP"); m.hp = whole(j, "hp"); m.experience = whole(j, "experience");
  m.isBoss = j.at("isBoss"); m.fled = j.at("fled"); m.charmed = j.at("charmed");
  m.status = j.at("status");
  return m;
}

json toJson(const Hero& h) {
  json eq = json::array();
  for (const auto& e : h.equipment) eq.push_back(e ? toJson(*e) : json(nullptr));
  json sc = json::array(), ch = json::array();
  for (const Item& s : h.scrolls) sc.push_back(toJson(s));
  for (const Monster& m : h.charmees) ch.push_back(toJson(m));
  return {{"name", h.name}, {"level", h.level}, {"experience", h.experience}, {"hp", h.hp}, {"maxHP", h.maxHP}, {"mp", h.mp},
          {"maxMP", h.maxMP}, {"ac", h.ac}, {"gold", h.gold}, {"score", h.score}, {"steps", h.steps}, {"base", stats(h.base)},
          {"charmees", ch}, {"equipment", eq}, {"scrolls", sc},
          {"startingItem", h.startingItem ? toJson(*h.startingItem) : json(nullptr)},
          {"invisible", h.invisible}, {"invisibleSince", h.invisibleSince}};
}
Hero heroOf(const json& j) {
  Hero h;
  h.name = j.at("name");
  h.level = whole(j, "level"); h.experience = whole(j, "experience", kSaneBig);
  h.base = statsOf(j.at("base"));
  for (const json& m : j.at("charmees")) h.charmees.push_back(monsterOf(m));
  const json& eq = j.at("equipment");
  if (!eq.is_array() || eq.size() != h.equipment.size()) throw std::runtime_error("bad equipment");
  for (size_t i = 0; i < eq.size(); ++i) if (!eq[i].is_null()) h.equipment[i] = itemOf(eq[i]);
  for (const json& s : j.at("scrolls")) h.scrolls.push_back(itemOf(s));
  if (!j.at("startingItem").is_null()) h.startingItem = itemOf(j.at("startingItem"));
  h.invisible = j.at("invisible");
  h.invisibleSince = j.at("invisibleSince");
  h.gold = whole(j, "gold", kSaneBig); h.score = whole(j, "score", kSaneBig); h.steps = whole(j, "steps", kSaneBig);
  h.recalc();  // the derived numbers (calc, maximums, AC) come from the stats and gear; then the saved current values go back
  h.maxHP = whole(j, "maxHP"); h.maxMP = whole(j, "maxMP"); h.ac = whole(j, "ac");
  h.hp = whole(j, "hp"); h.mp = whole(j, "mp");
  if (h.level < 1 || h.maxHP < 1 || h.hp < 0 || h.hp > h.maxHP || h.mp < 0 || h.mp > h.maxMP || h.gold < 0) throw std::runtime_error("bad hero");
  return h;
}

json toJson(const Ghost& g) {
  return {{"name", g.name}, {"floor", g.floor}, {"level", g.level}, {"gold", g.gold}, {"ac", g.ac}, {"maxHP", g.maxHP},
          {"experience", g.experience}, {"base", stats(g.base)}, {"loot", toJson(g.loot)}};
}
Ghost ghostOf(const json& j) {
  Ghost g;
  g.name = j.at("name");
  g.floor = whole(j, "floor"); g.level = whole(j, "level"); g.gold = whole(j, "gold", kSaneBig); g.ac = whole(j, "ac");
  g.maxHP = whole(j, "maxHP"); g.experience = whole(j, "experience", kSaneBig);
  g.base = statsOf(j.at("base"));
  g.loot = itemOf(j.at("loot"));
  return g;
}

json toJson(const FloorMemory& m) {
  std::string seen(m.seen.size(), '0');
  for (size_t i = 0; i < m.seen.size(); ++i)
    if (m.seen[i]) seen[i] = '1';
  json tiles = json::array();
  for (const auto& t : m.tiles) tiles.push_back(t);
  return {{"seen", seen}, {"tiles", tiles}, {"secretOpen", m.secretOpen}, {"secretRevealed", m.secretRevealed}};
}

FloorMemory floorOf(const json& j) {
  FloorMemory m;
  for (const char c : j.at("seen").get<std::string>()) m.seen.push_back(c == '1');
  for (const json& t : j.at("tiles")) m.tiles.push_back(t.get<std::array<int, 3>>());
  m.secretOpen = j.at("secretOpen");
  m.secretRevealed = j.at("secretRevealed");
  return m;
}

}  // namespace

std::string stateDir() {
  if (const char* env = std::getenv("LABYTERM_STATE_DIR"); env && *env) return env;
  if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg) return (fs::path(xdg) / "labyterm").string();
  if (const char* home = std::getenv("HOME")) return (fs::path(home) / ".local/state/labyterm").string();
  return {};
}

bool saveExists(const std::string& dir) { return !dir.empty() && fs::exists(fs::path(dir) / "save.json"); }

bool writeSave(const std::string& dir, const SaveData& d) {
  if (dir.empty()) return false;
  json fo = json::array();
  for (size_t i = 0; i < d.fountain.effects().size(); ++i) fo.push_back(static_cast<int>(d.fountain.effects()[i]));
  json shop = json::array();
  for (const Item& i : d.shop) shop.push_back(toJson(i));
  json floors = json::object();
  for (const auto& [f, m] : d.floors) floors[std::to_string(f)] = toJson(m);
  json j = {{"version", kSaveVersion}, {"seed", d.seed}, {"rng", d.rng}, {"floorSeeds", d.floorSeeds}, {"floors", floors}, {"hero", toJson(d.hero)},
            {"bossFloors", d.tracker.marked(LevelTracker::Kind::Boss)}, {"treasureFloors", d.tracker.marked(LevelTracker::Kind::Treasure)},
            {"fountainFloors", d.tracker.marked(LevelTracker::Kind::Fountain)}, {"secretFloors", d.tracker.marked(LevelTracker::Kind::Secret)}, {"shop", shop},
            {"fountainEffects", fo}, {"fountainDrunk", d.fountain.drunk()}, {"ghost", d.ghost ? toJson(*d.ghost) : json(nullptr)}};
  return writeJson(fs::path(dir) / "save.json", j);
}

LoadResult loadSave(const std::string& dir, SaveData& out) {
  const fs::path path = fs::path(dir) / "save.json";
  if (dir.empty() || !fs::exists(path)) return LoadResult::Missing;
  std::error_code ec;
  const auto j = readJson(path);
  try {
    if (!j || j->is_null()) throw std::runtime_error("unreadable");
    SaveData d;
    d.seed = j->at("seed");
    if (j->at("rng").is_string()) d.rng = j->at("rng").get<std::string>();
    d.floorSeeds = j->at("floorSeeds").get<std::vector<std::uint64_t>>();
    if (d.floorSeeds.size() != static_cast<size_t>(kFloorSeeds)) throw std::runtime_error("bad floor seeds");
    for (const auto& [key, m] : j->at("floors").items()) d.floors[std::stoi(key)] = floorOf(m);
    d.hero = heroOf(j->at("hero"));
    d.tracker.restore(LevelTracker::Kind::Boss, j->at("bossFloors").get<std::vector<int>>());
    d.tracker.restore(LevelTracker::Kind::Treasure, j->at("treasureFloors").get<std::vector<int>>());
    d.tracker.restore(LevelTracker::Kind::Fountain, j->at("fountainFloors").get<std::vector<int>>());
    if (j->contains("secretFloors")) d.tracker.restore(LevelTracker::Kind::Secret, j->at("secretFloors").get<std::vector<int>>());  // (saves from before it existed have none)
    for (const json& i : j->at("shop")) d.shop.push_back(itemOf(i));
    std::array<FountainEffect, Fountain::kColors> eff{};
    const auto ids = j->at("fountainEffects").get<std::vector<int>>();
    if (ids.size() != eff.size()) throw std::runtime_error("bad fountain");
    for (size_t i = 0; i < eff.size(); ++i) {
      if (ids[i] < 1 || ids[i] > 7) throw std::runtime_error("bad fountain");
      eff[i] = static_cast<FountainEffect>(ids[i]);
    }
    d.fountain.restore(eff, j->at("fountainDrunk").get<std::array<bool, Fountain::kColors>>());
    if (!j->at("ghost").is_null()) d.ghost = ghostOf(j->at("ghost"));
    out = std::move(d);
  } catch (const std::exception&) {
    fs::rename(path, fs::path(dir) / "save.corrupt.json", ec);  // keep it for a look, and do not trip over it again
    return LoadResult::Corrupt;
  }
  fs::remove(path, ec);  // used once: dying, or quitting without saving again, loses it, as in the original
  return LoadResult::Ok;
}

bool writeGhost(const std::string& dir, const Ghost& g) {
  if (dir.empty()) return false;
  return writeJson(fs::path(dir) / "graveyard.json", {{"version", kSaveVersion}, {"ghost", toJson(g)}});
}

std::optional<Ghost> takeGhost(const std::string& dir) {
  if (dir.empty()) return std::nullopt;
  const fs::path path = fs::path(dir) / "graveyard.json";
  const auto j = readJson(path);
  std::error_code ec;
  fs::remove(path, ec);
  if (!j || j->is_null()) return std::nullopt;
  try {
    return ghostOf(j->at("ghost"));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

int insertScore(std::vector<ScoreEntry>& list, const ScoreEntry& e) {
  if (e.score <= 0) return 0;
  size_t at = list.size();
  for (size_t i = 0; i < list.size(); ++i)
    if (e.score > list[i].score) {
      at = i;
      break;
    }
  if (at >= kLegends) return 0;
  list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), e);
  if (list.size() > kLegends) list.resize(kLegends);
  return static_cast<int>(at) + 1;
}

bool recordLaunchMode(const std::string& dir, bool terminal) {
  if (dir.empty()) return false;
  const fs::path path = fs::path(dir) / "modes.json";
  bool window = false, term = false;
  if (const auto j = readJson(path); j && j->is_object()) {
    window = j->value("window", false);
    term = j->value("terminal", false);
  }
  (terminal ? term : window) = true;
  writeJson(path, {{"version", kSaveVersion}, {"window", window}, {"terminal", term}});
  return window && term;
}

std::vector<ScoreEntry> loadScores(const std::string& dir) {
  std::vector<ScoreEntry> out;
  if (dir.empty()) return out;
  const auto j = readJson(fs::path(dir) / "scores.json");
  if (!j || j->is_null()) return out;
  try {
    for (const json& e : j->at("entries")) {
      ScoreEntry s;
      s.name = e.at("name"); s.score = whole(e, "score", kSaneBig); s.floor = whole(e, "floor"); s.level = whole(e, "level"); s.steps = whole(e, "steps", kSaneBig);
      out.push_back(s);
    }
  } catch (const std::exception&) {
    out.clear();
  }
  if (out.size() > kLegends) out.resize(kLegends);
  return out;
}

int recordScore(const std::string& dir, const ScoreEntry& e) {
  if (dir.empty()) return 0;
  std::vector<ScoreEntry> list = loadScores(dir);
  const int rank = insertScore(list, e);
  if (rank == 0) return 0;
  json arrj = json::array();
  for (const ScoreEntry& s : list) arrj.push_back({{"name", s.name}, {"score", s.score}, {"floor", s.floor}, {"level", s.level}, {"steps", s.steps}});
  return writeJson(fs::path(dir) / "scores.json", {{"version", kSaveVersion}, {"entries", arrj}}) ? rank : 0;
}

}  // namespace laby
