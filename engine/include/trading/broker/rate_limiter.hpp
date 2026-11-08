#pragma once

#include "trading/journal/generated/records.hpp"

#include <array>
#include <cstdint>

namespace trading::broker {

enum class RequestPriority : std::uint8_t {
  Cancel = 1,
  Reduction = 2,
  NewEntry = 3,
};

// DESIGN §9.4 — token buckets per endpoint category; cancels win over entries.
class CategoryRateLimiter {
public:
  void configure(journal::v1::EndpointCategory category, double capacity, double refill_per_second);

  [[nodiscard]] bool admit(journal::v1::EndpointCategory category, RequestPriority priority,
                           std::int64_t now_mono_ns);
  void on_rate_limited(journal::v1::EndpointCategory category, std::int64_t now_mono_ns,
                       std::int64_t backoff_ns);

  [[nodiscard]] std::int64_t category_backoff_until(journal::v1::EndpointCategory category) const;

private:
  struct Bucket {
    double capacity{0.0};
    double refill_per_ns{0.0};
    double tokens{0.0};
    std::int64_t last_refill_ns{0};
    std::int64_t backoff_until_ns{0};
  };

  [[nodiscard]] Bucket& bucket_for(journal::v1::EndpointCategory category) noexcept;
  [[nodiscard]] const Bucket& bucket_for(journal::v1::EndpointCategory category) const noexcept;
  void refill(Bucket& bucket, std::int64_t now_mono_ns) noexcept;
  [[nodiscard]] bool try_consume(Bucket& bucket, double cost) noexcept;

  std::array<Bucket, 3> buckets_{};
};

[[nodiscard]] journal::v1::EndpointCategory category_for_place(bool reduce_only) noexcept;
[[nodiscard]] journal::v1::EndpointCategory category_for_cancel() noexcept;
[[nodiscard]] journal::v1::EndpointCategory category_for_query() noexcept;

} // namespace trading::broker
