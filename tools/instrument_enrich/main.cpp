// T-0104: instrument master enrichment CLI (config/analysis only).

#include "enrich.hpp"
#include "instrument_load.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return 2;
}

void print_report(const trading::instruments::EnrichmentReport& report) {
  std::cout << "mapped=" << report.mapped_symbols.size()
            << " missing=" << report.missing_symbols.size()
            << " missing_instrument_id=" << report.missing_instrument_id.size() << '\n';
  for (const auto& symbol : report.missing_symbols) {
    std::cout << "missing_symbol: " << symbol << '\n';
  }
  for (const auto& symbol : report.missing_instrument_id) {
    std::cout << "missing_instrument_id: " << symbol << '\n';
  }
}

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path seed_path;
  std::filesystem::path patch_path;
  std::filesystem::path symbols_path;
  std::filesystem::path output_path;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--seed" && i + 1 < argc) {
      seed_path = argv[++i];
      continue;
    }
    if (arg == "--patch" && i + 1 < argc) {
      patch_path = argv[++i];
      continue;
    }
    if (arg == "--symbols" && i + 1 < argc) {
      symbols_path = argv[++i];
      continue;
    }
    if (arg == "--output" && i + 1 < argc) {
      output_path = argv[++i];
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: instrument_enrich --seed <toml> --symbols <file> "
                   "[--patch <toml>] [--output <toml>]\n";
      return 0;
    }
    return fail("unknown argument: " + arg);
  }

  if (seed_path.empty() || symbols_path.empty()) {
    return fail("--seed and --symbols are required");
  }

  auto seed = trading::instruments::load_instrument_master_file(seed_path);
  if (!seed) {
    return fail(seed.error().message);
  }

  trading::instruments::InstrumentMaster master = seed.value();
  if (!patch_path.empty()) {
    auto patch = trading::instruments::load_instrument_master_file(patch_path);
    if (!patch) {
      return fail(patch.error().message);
    }
    auto merged =
        trading::instruments::merge_masters_by_symbol(master, patch.value());
    if (!merged) {
      return fail(merged.error().message);
    }
    master = std::move(merged.value());
  }

  auto symbols = trading::instruments::load_symbol_list_file(symbols_path);
  if (!symbols) {
    return fail(symbols.error().message);
  }

  auto report =
      trading::instruments::build_enrichment_report(master, symbols.value());
  if (!report) {
    return fail(report.error().message);
  }

  print_report(report.value());

  if (!output_path.empty()) {
    if (!report.value().missing_symbols.empty() ||
        !report.value().missing_instrument_id.empty()) {
      return fail("refusing to write output: unresolved symbols or instrument_id gaps");
    }
    const auto written = trading::instruments::write_instrument_master_toml(
        report.value().subset, output_path);
    if (!written) {
      return fail(written.error().message);
    }
    std::cout << "wrote " << output_path.string() << '\n';
  }

  if (!report.value().missing_symbols.empty() ||
      !report.value().missing_instrument_id.empty()) {
    return 1;
  }
  return 0;
}
