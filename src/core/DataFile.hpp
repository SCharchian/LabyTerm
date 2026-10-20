#pragma once
#include <string>

#include <nlohmann/json.hpp>

namespace laby {

// Directory holding the game's data. $LABYTERM_DATA, else <exe>/../share/labyterm (installed), else the source tree's data/.
std::string dataDir();

// Loads <dataDir>/<relativePath>. Throws std::runtime_error with the file name and
// parse position on failure, so bad data fails loudly at startup.
nlohmann::json loadDataFile(const std::string& relativePath);

// Parses an absolute file path as JSON (// and /* */ comments allowed). Same error behavior.
nlohmann::json loadJsonPath(const std::string& absolutePath);

}  // namespace laby
