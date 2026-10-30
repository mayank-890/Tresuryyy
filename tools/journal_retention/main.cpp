// T-0105: journal retention / rotation ops CLI (recording infrastructure only).

#include "trading/journal/rotation.hpp"
#include "trading/journal/segment_footer.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return 2;
}

void print_status(const std::filesystem::path& root) {
  const auto segments = trading::journal::scan_journal_segments(root);
  std::cout << "root=" << root << " segments=" << segments.size() << '\n';
  for (const auto& seg : segments) {
    std::cout << (seg.closed ? "closed" : "open") << " first_seq=" << seg.first_sequence
              << " records=" << seg.record_count << " last_seq=" << seg.last_sequence
              << " bytes=" << seg.size_bytes << " path=" << seg.path.filename().string() << '\n';
  }
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    return fail("usage: journal_retention status --root PATH | apply --root PATH "
                "[--max-closed-segments N] [--max-closed-age-sec S]");
  }

  const std::string command = argv[1];
  std::filesystem::path root;
  trading::journal::RetentionPolicy policy{};

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--root" && i + 1 < argc) {
      root = argv[++i];
    } else if (arg == "--max-closed-segments" && i + 1 < argc) {
      policy.max_closed_segments = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
    } else if (arg == "--max-closed-age-sec" && i + 1 < argc) {
      const auto sec = std::strtoull(argv[++i], nullptr, 10);
      policy.max_closed_age_ns = sec * 1'000'000'000ULL;
    } else {
      return fail("unknown argument: " + arg);
    }
  }

  if (root.empty()) {
    const char* env_root = std::getenv("TRADELAB_JOURNAL_ROOT");
    if (env_root != nullptr) {
      root = env_root;
    }
  }
  if (root.empty()) {
    return fail("--root or TRADELAB_JOURNAL_ROOT is required");
  }

  if (command == "status") {
    print_status(root);
    return 0;
  }

  if (command == "apply") {
    const auto result = trading::journal::apply_retention(root, policy, false);
    if (!result.is_ok()) {
      return fail("retention apply failed");
    }
    const auto& applied = result.value();
    if (!applied.archive_verified && applied.candidates > 0) {
      std::cerr << "refused: set ARCHIVE_VERIFIED=1 after archive verification\n";
      std::cerr << "candidates=" << applied.candidates << '\n';
      return 1;
    }
    std::cout << "removed=" << applied.removed << " candidates=" << applied.candidates << '\n';
    return 0;
  }

  return fail("unknown command: " + command);
}
