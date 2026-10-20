#include "core/DataFile.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace laby {

namespace {

// An installed copy keeps its data in <prefix>/share/labyterm, beside <prefix>/bin/labyterm. Empty if there is none
// (a development build runs from build/, where ../share/labyterm does not exist).
std::string installedDataDir() {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (ec) return {};
  const fs::path dir = exe.parent_path().parent_path() / "share/labyterm";
  if (fs::exists(dir / "gameplay.json", ec)) return dir.string();
  return {};
}

}  // namespace

std::string dataDir() {
  if (const char* env = std::getenv("LABYTERM_DATA")) return env;
  static const std::string installed = installedDataDir();
  if (!installed.empty()) return installed;
  return LABYTERM_DATA_DIR;
}

nlohmann::json loadJsonPath(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open file: " + path);
  try {
    return nlohmann::json::parse(f, nullptr, true, /*ignore_comments=*/true);
  } catch (const nlohmann::json::parse_error& e) {
    throw std::runtime_error(path + ": " + e.what());
  }
}

nlohmann::json loadDataFile(const std::string& rel) { return loadJsonPath(dataDir() + "/" + rel); }

}  // namespace laby
