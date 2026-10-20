#include "core/Tuning.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "core/DataFile.hpp"

namespace laby {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

Tuning gActive;
bool gHaveActive = false;

// ---- typed setters, keyed by knob path ---------------------------------------------------------

using Setter = std::function<void(const json& value, const std::string& path)>;

double asNumber(const json& v, const std::string& path) {
  if (!v.is_number()) throw std::runtime_error(path + ": expected a number, got " + v.dump());
  return v.get<double>();
}

void setStatTable(StatTable& t, const json& v, const std::string& path) {
  if (!v.is_object()) throw std::runtime_error(path + ": expected an object like {\"STR\": 1}");
  for (auto& [k, x] : v.items()) {
    auto stat = fromString<Stat>(k);
    if (!stat) throw std::runtime_error(path + "." + k + ": unknown stat (use STR, DEX, INT, VIT, CHA)");
    t[static_cast<size_t>(*stat)] = asNumber(x, path + "." + k);
  }
}

struct Registrar {
  std::map<std::string, Setter> setters;
  void operator()(const char* path, double& field) {
    setters[path] = [&field](const json& v, const std::string& p) { field = asNumber(v, p); };
  }
  void operator()(const char* path, int& field) {
    setters[path] = [&field](const json& v, const std::string& p) {
      double d = asNumber(v, p);
      if (d != static_cast<double>(static_cast<long long>(d)))
        throw std::runtime_error(p + ": must be a whole number (it is an integer knob)");
      field = static_cast<int>(d);
    };
  }
  void operator()(const char* path, StatTable& field) {
    setters[path] = [&field](const json& v, const std::string& p) { setStatTable(field, v, p); };
  }
  void operator()(const char* path, ClassStatTable& field) {
    setters[path] = [&field](const json& v, const std::string& p) {
      if (!v.is_object()) throw std::runtime_error(p + ": expected an object keyed by monster class");
      for (auto& [k, x] : v.items()) {
        auto c = fromString<MonsterClass>(k);
        if (!c) throw std::runtime_error(p + "." + k + ": unknown monster class");
        setStatTable(field[static_cast<size_t>(*c)], x, p + "." + k);
      }
    };
  }
};

// ---- helpers ----------------------------------------------------------------------------------

int editDistance(const std::string& a, const std::string& b) {
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = static_cast<int>(i);
    for (size_t j = 1; j <= b.size(); ++j)
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] != b[j - 1])});
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

std::string suggest(const std::string& path, const std::map<std::string, Setter>& setters) {
  std::string best;
  int bestD = 1 << 30;
  auto consider = [&](const std::string& cand) {
    int d = editDistance(path, cand);
    if (d < bestD) { bestD = d; best = cand; }
  };
  for (auto& [p, s] : setters) {
    consider(p);
    for (size_t dot = p.find('.'); dot != std::string::npos; dot = p.find('.', dot + 1))
      consider(p.substr(0, dot));  // group names too
  }
  return bestD <= std::max<int>(3, static_cast<int>(path.size()) / 4) ? " (did you mean '" + best + "'?)" : "";
}

bool hasParamUnder(const std::string& prefix, const std::map<std::string, Setter>& setters) {
  auto it = setters.lower_bound(prefix + ".");
  return it != setters.end() && it->first.compare(0, prefix.size() + 1, prefix + ".") == 0;
}

// Walks a (partial) tuning document: each leaf path must be a known knob. `visit` is called with
// the knob path and its JSON value. Throws on unknown keys.
void walk(const json& node, const std::string& prefix, const std::map<std::string, Setter>& setters,
          const std::function<void(const std::string&, const json&)>& visit) {
  if (!node.is_object()) throw std::runtime_error("tuning must be a JSON object" + (prefix.empty() ? "" : " at " + prefix));
  for (auto& [key, value] : node.items()) {
    std::string path = prefix.empty() ? key : prefix + "." + key;
    if (setters.count(path)) {
      visit(path, value);
    } else if (value.is_object() && hasParamUnder(path, setters)) {
      walk(value, path, setters, visit);
    } else {
      throw std::runtime_error("unknown tuning key '" + path + "'" + suggest(path, setters));
    }
  }
}

fs::path defaultsPath() { return fs::path(dataDir()) / "gameplay.json"; }

std::string fmt(double d) {
  std::ostringstream o;
  o << d;
  return o.str();
}

std::string describeStatTable(const StatTable& t) {
  std::string s = "{";
  for (int i = 0; i < kStatCount; ++i)
    if (t[i] != 0) s += std::string(toString(static_cast<Stat>(i))) + ":" + fmt(t[i]) + " ";
  if (s.size() > 1) s.pop_back();
  return s + "}";
}

struct Describer {
  std::vector<std::pair<std::string, std::string>> rows;
  void operator()(const char* path, const double& v) { rows.emplace_back(path, fmt(v)); }
  void operator()(const char* path, const int& v) { rows.emplace_back(path, std::to_string(v)); }
  void operator()(const char* path, const StatTable& v) { rows.emplace_back(path, describeStatTable(v)); }
  void operator()(const char* path, const ClassStatTable& v) {
    std::string s;
    for (int c = 0; c < kMonsterClassCount; ++c)
      s += std::string(toString(static_cast<MonsterClass>(c))) + describeStatTable(v[c]) + " ";
    if (!s.empty()) s.pop_back();
    rows.emplace_back(path, s);
  }
};

long long stampOf(const fs::path& p) {
  std::error_code ec;
  auto t = fs::last_write_time(p, ec);
  return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
}

}  // namespace

const Tuning& tuning() {
  if (!gHaveActive) {
    setTuning(loadTuning());
  }
  return gActive;
}

std::string tuningOverridePath() {
  if (const char* env = std::getenv("LABYTERM_TUNING")) return env;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return (fs::path(xdg) / "labyterm/tuning.json").string();
  if (const char* home = std::getenv("HOME")) return (fs::path(home) / ".config/labyterm/tuning.json").string();
  return {};
}

TuningLoad loadTuning() {
  TuningLoad out;
  Registrar reg;
  forEachParam(out.values, reg);

  // 1) defaults: gameplay.json must define every knob and nothing unknown
  json defaults = loadJsonPath(defaultsPath().string());
  std::map<std::string, bool> seen;
  try {
    walk(defaults, "", reg.setters, [&](const std::string& path, const json& v) {
      reg.setters.at(path)(v, path);
      seen[path] = true;
    });
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("data/gameplay.json: ") + e.what() +
                             " (if you added or renamed a knob, run `python3 tools/gen_tuning.py` and rebuild)");
  }
  for (auto& [path, setter] : reg.setters)
    if (!seen.count(path))
      throw std::runtime_error("data/gameplay.json is missing knob '" + path +
                               "' (run `python3 tools/gen_tuning.py` and rebuild)");

  // 2) optional per-user overrides (applied on a copy so a bad file changes nothing)
  std::string op = tuningOverridePath();
  std::error_code ec;
  if (!op.empty() && fs::exists(op, ec)) {
    try {
      json over = loadJsonPath(op);
      TuningLoad trial = out;
      Registrar treg;
      forEachParam(trial.values, treg);
      walk(over, "", treg.setters, [&](const std::string& path, const json& v) {
        treg.setters.at(path)(v, path);
        trial.overridden.push_back(path);
      });
      out.values = trial.values;
      out.overridden = trial.overridden;
    } catch (const std::exception& e) {
      out.overrideError = e.what();
    }
  }
  return out;
}

void setTuning(const TuningLoad& load) {
  gActive = load.values;
  gHaveActive = true;
}

std::string describeTuning(const TuningLoad& load) {
  Tuning copy = load.values;
  Describer d;
  forEachParam(copy, d);
  std::map<std::string, std::string> docs;
  for (auto& pd : kTuningDocs) docs[std::string(pd.path)] = std::string(pd.doc);

  std::ostringstream o;
  std::string group;
  for (auto& [path, value] : d.rows) {
    std::string g = path.substr(0, path.find('.'));
    if (g != group) { group = g; o << "\n[" << g << "]\n"; }
    bool over = std::find(load.overridden.begin(), load.overridden.end(), path) != load.overridden.end();
    o << (over ? " * " : "   ") << path.substr(path.find('.') + 1) << " = " << value << "\n";
    o << "       " << docs[path] << "\n";
  }
  o << "\n" << kTuningParamCount << " knobs; " << load.overridden.size() << " overridden (marked *) by " << tuningOverridePath() << "\n";
  if (!load.overrideError.empty()) o << "OVERRIDE FILE REJECTED: " << load.overrideError << "\n";
  return o.str();
}

bool TuningWatcher::poll(std::uint64_t nowMs) {
  if (primed_ && nowMs - lastCheckMs_ < intervalMs_) return false;
  lastCheckMs_ = nowMs;
  long long stamp = stampOf(defaultsPath()) + 31 * stampOf(tuningOverridePath());
  std::error_code ec;
  if (fs::exists(tuningOverridePath(), ec)) stamp += 1;  // file appearing/disappearing counts as a change
  if (!primed_) {
    primed_ = true;
    stamp_ = stamp;
    TuningLoad l = loadTuning();
    overridden_ = static_cast<int>(l.overridden.size());
    error_ = l.overrideError;
    setTuning(l);  // startup: a rejected override means plain defaults (error_ says why)
    if (!error_.empty()) std::cerr << "labyterm: tuning file rejected, using defaults: " << error_ << "\n";
    return false;
  }
  if (stamp == stamp_) return false;
  stamp_ = stamp;
  try {
    TuningLoad l = loadTuning();
    error_ = l.overrideError;
    if (!error_.empty()) std::cerr << "labyterm: tuning file rejected, keeping the last good values: " << error_ << "\n";
    if (l.overrideError.empty()) {
      overridden_ = static_cast<int>(l.overridden.size());
      setTuning(l);  // a rejected edit keeps the last good values active
    }
  } catch (const std::exception& e) {
    error_ = e.what();  // broken defaults file: keep what we have
  }
  return true;
}

}  // namespace laby
