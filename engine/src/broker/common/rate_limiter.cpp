#include "trading/broker/rate_limiter.hpp"

#include <algorithm>

namespace trading::broker {

namespace {

[[nodiscard]] std::size_t bucket_index(const journal::v1::EndpointCategory category) noexcept {
  switch (category) {
  case journal::v1::EndpointCategory::OrderEntry:
    return 0;
  case journal::v1::EndpointCategory::OrderQuery:
    return 1;
  case journal::v1::EndpointCategory::Cancel:
    return 2;
  }
  return 0;
}

} // namespace

CategoryRateLimiter::Bucket& CategoryRateLimiter::bucket_for(
    const journal::v1::EndpointCategory category) noexcept {
  return buckets_[bucket_index(category)];
}

const CategoryRateLimiter::Bucket& CategoryRateLimiter::bucket_for(
    const journal::v1::EndpointCategory category) const noexcept {
  return buckets_[bucket_index(category)];
}

void CategoryRateLimiter::configure(const journal::v1::EndpointCategory category, const double capacity,
                                    const double refill_per_second) {
  Bucket& bucket = bucket_for(category);
  bucket.capacity = capacity;
  bucket.tokens = capacity;
  bucket.refill_per_ns = refill_per_second / 1'000'000'000.0;
  bucket.last_refill_ns = 0;
}

void CategoryRateLimiter::refill(Bucket& bucket, const std::int64_t now_mono_ns) noexcept {
  if (bucket.last_refill_ns == 0) {
    bucket.last_refill_ns = now_mono_ns;
    return;
  }
  const std::int64_t elapsed = now_mono_ns - bucket.last_refill_ns;
  if (elapsed <= 0) {
    return;
  }
  bucket.tokens = std::min(bucket.capacity, bucket.tokens + (static_cast<double>(elapsed) * bucket.refill_per_ns));
  bucket.last_refill_ns = now_mono_ns;
}

bool CategoryRateLimiter::try_consume(Bucket& bucket, const double cost) noexcept {
  if (bucket.tokens + 1e-9 < cost) {
    return false;
  }
  bucket.tokens -= cost;
  return true;
}

bool CategoryRateLimiter::admit(const journal::v1::EndpointCategory category,
                                 const RequestPriority priority, const std::int64_t now_mono_ns) {
  if (now_mono_ns < category_backoff_until(category)) {
    return false;
  }

  Bucket& primary = bucket_for(category);
  refill(primary, now_mono_ns);

  if (priority == RequestPriority::Cancel) {
    Bucket& cancel_bucket = bucket_for(journal::v1::EndpointCategory::Cancel);
    refill(cancel_bucket, now_mono_ns);
    if (try_consume(cancel_bucket, 1.0)) {
      return true;
    }
    return try_consume(primary, 1.0);
  }

  if (priority == RequestPriority::Reduction) {
    if (try_consume(primary, 1.0)) {
      return true;
    }
    const Bucket& cancel_bucket = bucket_for(journal::v1::EndpointCategory::Cancel);
    return cancel_bucket.tokens > primary.tokens && try_consume(primary, 1.0);
  }

  return try_consume(primary, 1.0);
}

void CategoryRateLimiter::on_rate_limited(const journal::v1::EndpointCategory category,
                                         const std::int64_t now_mono_ns, const std::int64_t backoff_ns) {
  Bucket& bucket = bucket_for(category);
  bucket.backoff_until_ns = std::max(bucket.backoff_until_ns, now_mono_ns + backoff_ns);
  bucket.tokens = 0.0;
}

std::int64_t CategoryRateLimiter::category_backoff_until(
    const journal::v1::EndpointCategory category) const {
  return bucket_for(category).backoff_until_ns;
}

journal::v1::EndpointCategory category_for_place(const bool reduce_only) noexcept {
  (void)reduce_only;
  return journal::v1::EndpointCategory::OrderEntry;
}

journal::v1::EndpointCategory category_for_cancel() noexcept {
  return journal::v1::EndpointCategory::Cancel;
}

journal::v1::EndpointCategory category_for_query() noexcept {
  return journal::v1::EndpointCategory::OrderQuery;
}

} // namespace trading::broker
