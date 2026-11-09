#pragma once

#include <filesystem>

namespace trading::trader {

// Paper profiles use tests/mock_exchange/scenarios/paper_pipeline.toml (TI §12.3).
[[nodiscard]] std::filesystem::path default_paper_mock_scenario(
    const std::filesystem::path& config_root);

} // namespace trading::trader
