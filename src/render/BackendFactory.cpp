#include "render/Backend.hpp"

namespace laby {

BackendKind& backendKind() {
  static BackendKind kind = BackendKind::Window;
  return kind;
}

std::unique_ptr<Backend> makeBackend(int gridW, int gridH, const char* title) {
  return backendKind() == BackendKind::Terminal ? makeTerminalBackend(gridW, gridH) : makeSdlBackend(gridW, gridH, title);
}

}  // namespace laby
