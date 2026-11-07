#include "trading/portfolio/types.hpp"

namespace trading::portfolio {

bool client_id_less(const orders::ClientOrderId& lhs, const orders::ClientOrderId& rhs) noexcept {
  for (std::size_t i = 0; i < orders::kClientIdStorage; ++i) {
    const unsigned char a = static_cast<unsigned char>(lhs.bytes[i]);
    const unsigned char b = static_cast<unsigned char>(rhs.bytes[i]);
    if (a < b) {
      return true;
    }
    if (a > b) {
      return false;
    }
  }
  return false;
}

} // namespace trading::portfolio
