#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace trading::strategy {

// Non-owning view of one immutable parameter snapshot.
// Valid only while the Params object passed to param_view() is alive.
struct ParamView {
  std::uint64_t version{0};
  std::span<const std::byte> bytes{};
};

template <typename Params> [[nodiscard]] ParamView param_view(const Params& params) {
  static_assert(std::is_trivially_copyable_v<Params>);
  return ParamView{params.version, std::as_bytes(std::span<const Params>(&params, 1))};
}

// Two immutable slots. load_for_cycle() copies the published slot.
// That copy is the decision-cycle boundary: later publish() calls do not
// change it. publish() runs between cycles, not during load_for_cycle().
// A second publish may reuse a slot that an in-progress copy is still reading.
template <typename Params> class ParamDoubleBuffer {
public:
  static_assert(std::is_trivially_copyable_v<Params>);

  ParamDoubleBuffer() = default;

  void publish(const Params& next) {
    // Relaxed: only this publisher writes the index. The slot write happens
    // before the release store below.
    const auto current = published_.load(std::memory_order_relaxed);
    const auto inactive = current ^ std::uint32_t{1};
    slots_[inactive] = next;
    // Release: the slot write is visible to a reader that acquire-loads the index.
    published_.store(inactive, std::memory_order_release);
  }

  [[nodiscard]] Params load_for_cycle() const {
    // Acquire: pairs with publish's release so the copied slot is complete.
    const auto index = published_.load(std::memory_order_acquire);
    return slots_[index];
  }

private:
  Params slots_[2]{};
  // Index of the slot readers may copy. Not a mutex.
  alignas(64) std::atomic<std::uint32_t> published_{0};
};

} // namespace trading::strategy
