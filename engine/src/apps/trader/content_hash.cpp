#include "content_hash.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace trading::config {

void sort_json_keys(nlohmann::json& value) {
  if (value.is_object()) {
    std::vector<std::string> keys;
    keys.reserve(value.size());
    for (auto it = value.begin(); it != value.end(); ++it) {
      keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());
    nlohmann::json sorted = nlohmann::json::object();
    for (const auto& key : keys) {
      nlohmann::json child = value.at(key);
      sort_json_keys(child);
      sorted[key] = std::move(child);
    }
    value = std::move(sorted);
  } else if (value.is_array()) {
    for (auto& element : value) {
      sort_json_keys(element);
    }
  }
}

namespace {

std::string sha256_hex(std::string_view data) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_len = 0;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx == nullptr) {
    throw std::runtime_error("EVP_MD_CTX_new failed");
  }
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx, data.data(), data.size()) != 1 ||
      EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) != 1) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("SHA-256 failed");
  }
  EVP_MD_CTX_free(ctx);
  std::ostringstream oss;
  for (unsigned int i = 0; i < digest_len; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
  }
  return oss.str();
}

} // namespace

std::string profile_content_hash_hex(const nlohmann::json& profile_document) {
  nlohmann::json copy = profile_document;
  sort_json_keys(copy);
  const std::string canonical = copy.dump();
  return sha256_hex(canonical);
}

} // namespace trading::config
