#include <array>
#include <span>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "strategies/flat_echo/strategy.hpp"

namespace py = pybind11;

namespace {

constexpr std::size_t kMaxIntents = 8;

} // namespace

// SAMPLE bindings for research. Not a live order path.
// pybind11 is recorded under ADR-0005 and Q-24. This module is built only
// when TRADING_BUILD_BINDINGS=ON.
PYBIND11_MODULE(trading_bindings, module) {
  using trading::strategy::MarketSnapshot;
  using trading::strategy::OrderIntent;
  using trading::strategy::Quote;
  using trading::strategy::Side;
  using trading::strategy::TimeInForce;
  using trading::strategy::flat_echo::FlatEcho;
  using trading::strategy::flat_echo::FlatParams;
  using trading::strategy::param_view;

  module.doc() = "SAMPLE strategy bindings. Not for trading.";

  py::enum_<Side>(module, "Side").value("Buy", Side::Buy).value("Sell", Side::Sell);

  py::enum_<TimeInForce>(module, "TimeInForce")
      .value("Day", TimeInForce::Day)
      .value("ImmediateOrCancel", TimeInForce::ImmediateOrCancel)
      .value("FillOrKill", TimeInForce::FillOrKill);

  py::class_<Quote>(module, "Quote")
      .def(py::init<>())
      .def_readwrite("instrument_id", &Quote::instrument_id)
      .def_readwrite("bid_ticks", &Quote::bid_ticks)
      .def_readwrite("ask_ticks", &Quote::ask_ticks)
      .def_readwrite("bid_size_lots", &Quote::bid_size_lots)
      .def_readwrite("ask_size_lots", &Quote::ask_size_lots)
      .def_readwrite("exchange_time_ns", &Quote::exchange_time_ns);

  py::class_<FlatParams>(module, "FlatParams")
      .def(py::init<>())
      .def_readwrite("version", &FlatParams::version)
      .def_readwrite("instrument_id", &FlatParams::instrument_id)
      .def_readwrite("qty_lots", &FlatParams::qty_lots)
      .def_readwrite("side", &FlatParams::side)
      .def_readwrite("time_in_force", &FlatParams::time_in_force);

  py::class_<OrderIntent>(module, "OrderIntent")
      .def_readonly("strategy_id", &OrderIntent::strategy_id)
      .def_readonly("instrument_id", &OrderIntent::instrument_id)
      .def_readonly("side", &OrderIntent::side)
      .def_readonly("qty_lots", &OrderIntent::qty_lots)
      .def_readonly("price_ticks", &OrderIntent::price_ticks)
      .def_readonly("time_in_force", &OrderIntent::time_in_force)
      .def_readonly("param_version", &OrderIntent::param_version)
      .def_readonly("snapshot_sequence", &OrderIntent::snapshot_sequence);

  py::class_<FlatEcho>(module, "FlatEcho")
      .def(py::init<std::uint32_t>())
      .def("name", [](const FlatEcho& self) { return std::string{self.name()}; })
      .def("strategy_id", &FlatEcho::strategy_id)
      .def("decide",
           [](const FlatEcho& self, std::uint64_t sequence, const std::vector<Quote>& quotes,
              const FlatParams& params) {
             std::array<OrderIntent, kMaxIntents> out{};
             const MarketSnapshot snapshot{sequence, std::span<const Quote>{quotes}};
             const auto written = self.decide(snapshot, param_view(params), out);
             const auto count = static_cast<std::ptrdiff_t>(written);
             return std::vector<OrderIntent>(out.begin(), out.begin() + count);
           });
}
