#include "paper_paths.hpp"

namespace trading::trader {

std::filesystem::path default_paper_mock_scenario(const std::filesystem::path& config_root) {
  std::filesystem::path repo_root = config_root;
  if (repo_root.filename() == "config") {
    repo_root = repo_root.parent_path();
  }
  return repo_root / "tests/mock_exchange/scenarios/paper_pipeline.toml";
}

} // namespace trading::trader
