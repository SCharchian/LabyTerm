#include "render/TermCaps.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "render/TermOut.hpp"

namespace laby {

std::string termQueries() {
  return "\x1b[48;2;1;2;3m"   // a truecolor background, 1 2 3
         "\x1bP$qm\x1b\\"     // DECRQSS: what is the SGR state? (a truecolor terminal echoes the colour back)
         "\x1b[0m"
         "\x1b[>0q"           // XTVERSION: the name and version
         "\x1b[c";            // primary device attributes: every terminal answers, so this ends the wait
}

namespace {

// Splits an SGR parameter list ("0;48:2::1:2:3") on ';' and ':' without the empty parts.
std::vector<std::string> sgrTokens(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ';' || c == ':') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool echoesTruecolor(const std::string& sgr) {
  const std::vector<std::string> t = sgrTokens(sgr);
  for (size_t i = 0; i + 4 < t.size(); ++i)
    if (t[i] == "48" && t[i + 1] == "2" && t[i + 2] == "1" && t[i + 3] == "2" && t[i + 4] == "3") return true;
  return false;
}

}  // namespace

TermReplies parseTermReplies(const std::string& b) {
  TermReplies r;
  for (size_t i = 0; i + 1 < b.size(); ++i) {
    if (b[i] != '\x1b') continue;
    if (b[i + 1] == 'P') {  // a device control string, ended by ST (ESC \) or BEL
      size_t end = b.find("\x1b\\", i + 2);
      size_t stLen = 2;
      if (end == std::string::npos) {
        end = b.find('\a', i + 2);
        stLen = 1;
      }
      if (end == std::string::npos) continue;
      const std::string body = b.substr(i + 2, end - (i + 2));
      if (body.rfind(">|", 0) == 0) {
        r.xtversion = body.substr(2);
      } else if (body.rfind("1$r", 0) == 0) {  // a valid DECRQSS reply: "1$r<sgr>m"
        std::string sgr = body.substr(3);
        if (!sgr.empty() && sgr.back() == 'm') sgr.pop_back();
        if (echoesTruecolor(sgr)) r.truecolorSgr = true;
      }
      i = end + stLen - 1;
    } else if (b[i + 1] == '[' && i + 2 < b.size() && b[i + 2] == '?') {  // ESC [ ? ... c: the device attributes
      const size_t end = b.find_first_not_of("0123456789;", i + 3);
      if (end != std::string::npos && b[end] == 'c') r.answered = true;
    }
  }
  return r;
}

bool truecolorLikely(const std::string& colorterm, const TermReplies& r) {
  return colorterm == "truecolor" || colorterm == "24bit" || r.truecolorSgr;
}

bool tmuxTextHasRgb(const std::string& text) {
  std::string tok;
  for (size_t i = 0; i <= text.size(); ++i) {
    const char c = i < text.size() ? text[i] : ',';
    if (c == ',' || c == ':' || c == ' ' || c == '\n' || c == ';' || c == '=') {
      if (tok == "RGB" || tok == "Tc") return true;
      tok.clear();
    } else {
      tok += c;
    }
  }
  return false;
}

std::string terminalId(const TermReplies& r, const std::string& termProgram, const std::string& term) {
  if (!r.xtversion.empty()) return r.xtversion;
  return (termProgram.empty() ? "?" : termProgram) + "/" + (term.empty() ? "?" : term);
}

bool knownOctantTerminal(const std::string& id) {
  // checked with tests/octant_probe on these (version strings: "ghostty 1.3.1", "foot(1.28.0)"); any capitalisation
  std::string low = id;
  std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return low.rfind("ghostty", 0) == 0 || low.rfind("foot", 0) == 0;
}

std::optional<GlyphMode> loadGlyphChoice(const std::string& dir, const std::string& id) {
  try {
    std::ifstream in(std::filesystem::path(dir) / "terminal.json");
    if (!in) return std::nullopt;
    const nlohmann::json j = nlohmann::json::parse(in);
    if (j.value("version", 0) != 1 || j.value("terminal", std::string()) != id) return std::nullopt;  // another terminal: ask again
    const std::string g = j.value("glyphs", std::string());
    if (g == "octant") return GlyphMode::Octant;
    if (g == "quadrant") return GlyphMode::Quadrant;
  } catch (...) {
  }
  return std::nullopt;
}

void saveGlyphChoice(const std::string& dir, const std::string& id, GlyphMode mode) {
  try {
    std::filesystem::create_directories(dir);
    nlohmann::json j = {{"version", 1}, {"terminal", id}, {"glyphs", mode == GlyphMode::Octant ? "octant" : "quadrant"}};
    const std::filesystem::path path = std::filesystem::path(dir) / "terminal.json", tmp = path.string() + ".tmp";
    {
      std::ofstream out(tmp);
      out << j.dump(2) << "\n";
    }
    std::filesystem::rename(tmp, path);
  } catch (...) {
  }
}

std::string colorSwatch(int cells) {
  std::string s;
  for (int i = 0; i < cells; ++i) {
    const double t = cells > 1 ? static_cast<double>(i) / (cells - 1) : 0.0;
    appendBg(s, Rgb{static_cast<std::uint8_t>(255 * t), static_cast<std::uint8_t>(255 * (1 - t)), static_cast<std::uint8_t>(128 + 100 * std::sin(t * 6.28318))});
    s += ' ';
  }
  return s + "\x1b[0m";
}

// A ring with an X through it, 2*cols x 4*rows sub-pixels; one text row of it.
std::string sampleRing(GlyphMode mode, int cols, int rows, int row) {
  const int w = cols * 2, h = rows * 4;
  const auto ink = [&](int x, int y) {
    const double cx = w / 2.0, cy = h / 2.0, r = std::min(w, h) / 2.0 - 0.5, d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
    return (d <= r && d >= r - 2.0) || x == y * w / h || x + y * w / h == w - 1;
  };
  std::string s;
  appendFg(s, Rgb{240, 200, 90});
  appendBg(s, Rgb{20, 24, 40});
  for (int col = 0; col < cols; ++col) {
    unsigned pat = 0;
    for (int j = 0; j < 4; ++j)
      for (int i = 0; i < 2; ++i)
        if (ink(col * 2 + i, row * 4 + j)) pat |= 1u << (j * 2 + i);
    appendUtf8(s, mode == GlyphMode::Octant ? kOctantChar[pat] : quadrantChar(pat));
  }
  return s + "\x1b[0m";
}

}  // namespace laby
