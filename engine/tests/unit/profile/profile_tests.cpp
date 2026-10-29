#include "content_hash.hpp"
#include "profile_startup.hpp"
#include "promotion_verifier.hpp"
#include "secret_store.hpp"

#include <gtest/gtest.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr const char* kValidProfileBody = R"(
[profile]
name = "test"
mode = "paper"
engine_build_required = ">=0.1.0"

[execution]
adapter = "mock"
client_id_prefix_source = "counter"

[market_data]
instruments_file = "config/instruments/delta_crypto.toml"
calendar = "config/calendars/crypto_24x7.toml"
stale_threshold_ms = 3000

[journal]
root = "data/journal"
segment_max_mb = 256
group_commit_ms = 2

[risk]
limits_source = "compiled"
tighten_overrides_file = "config/risk/overrides.toml"

[database]
url_secret = "trading/pg/projector"

[control]
socket = "/tmp/trading/control.sock"
)";

struct Ed25519KeyPair {
  EVP_PKEY* private_key{};
  std::string public_pem;
};

Ed25519KeyPair generate_ed25519_keypair() {
  Ed25519KeyPair keys;
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  EXPECT_NE(ctx, nullptr);
  EXPECT_EQ(EVP_PKEY_keygen_init(ctx), 1);
  EXPECT_EQ(EVP_PKEY_keygen(ctx, &keys.private_key), 1);
  EVP_PKEY_CTX_free(ctx);

  BIO* bio = BIO_new(BIO_s_mem());
  EXPECT_EQ(PEM_write_bio_PUBKEY(bio, keys.private_key), 1);
  char* data = nullptr;
  const long len = BIO_get_mem_data(bio, &data);
  keys.public_pem.assign(data, static_cast<std::size_t>(len));
  BIO_free(bio);
  return keys;
}

std::string base64_encode(const unsigned char* data, std::size_t len) {
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO* bio = BIO_new(BIO_s_mem());
  bio = BIO_push(b64, bio);
  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(bio, data, static_cast<int>(len));
  BIO_flush(bio);
  char* out = nullptr;
  const long out_len = BIO_get_mem_data(bio, &out);
  std::string encoded(out, static_cast<std::size_t>(out_len));
  BIO_free_all(bio);
  return encoded;
}

std::string sign_payload(EVP_PKEY* private_key, std::string_view payload) {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EXPECT_EQ(EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, private_key), 1);
  std::size_t sig_len = 0;
  EXPECT_EQ(EVP_DigestSign(ctx, nullptr, &sig_len,
                           reinterpret_cast<const unsigned char*>(payload.data()),
                           payload.size()),
            1);
  std::string signature(sig_len, '\0');
  EXPECT_EQ(EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(signature.data()), &sig_len,
                           reinterpret_cast<const unsigned char*>(payload.data()),
                           payload.size()),
            1);
  signature.resize(sig_len);
  EVP_MD_CTX_free(ctx);
  return base64_encode(reinterpret_cast<const unsigned char*>(signature.data()), signature.size());
}

class ProfileFixture : public ::testing::Test {
protected:
  void SetUp() override {
    root = fs::temp_directory_path() / "profile_tests";
    fs::remove_all(root);
    fs::create_directories(root / "profiles");
    fs::create_directories(root / "schema");
    fs::copy_file(fs::path(TRADING_REPO_ROOT) / "config/schema/profile.schema.json",
                  root / "schema/profile.schema.json");
    fs::copy_file(fs::path(TRADING_REPO_ROOT) / "config/schema/promotion.schema.json",
                  root / "schema/promotion.schema.json");
    setenv("TRADING_SECRET_TRADING_PG_PROJECTOR", "test-secret-value", 1);
  }

  void write_profile(std::string_view name, std::string_view body) {
    std::ofstream out(root / "profiles" / (std::string(name) + ".toml"));
    out << body;
  }

  fs::path root;

  static std::string escape_for_toml(std::string_view pem) {
    std::string out = "\"";
    for (char ch : pem) {
      if (ch == '\n') {
        out += "\\n";
      } else if (ch == '"') {
        out += "\\\"";
      } else {
        out += ch;
      }
    }
    out += "\"";
    return out;
  }
};

} // namespace

TEST_F(ProfileFixture, TCCF01MissingNameFails) {
  write_profile("bad", R"(
[profile]
mode = "paper"
engine_build_required = ">=0.1.0"
[execution]
adapter = "mock"
client_id_prefix_source = "counter"
[market_data]
instruments_file = "x"
calendar = "y"
stale_threshold_ms = 1
[journal]
root = "data/journal"
segment_max_mb = 256
group_commit_ms = 2
[risk]
limits_source = "compiled"
tighten_overrides_file = "config/risk/overrides.toml"
[database]
url_secret = "trading/pg/projector"
[control]
socket = "/tmp/x.sock"
)");
  trading::config::StartupContext ctx;
  const auto err = trading::config::run_profile_startup(root, "bad", false, true, ctx);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, trading::config::ConfigErrorCode::ProfileMissingName);
}

TEST_F(ProfileFixture, TCCF02UnknownKeyFails) {
  std::string body = kValidProfileBody;
  body += "\n[unknown]\nfoo = 1\n";
  write_profile("bad", body);
  trading::config::StartupContext ctx;
  const auto err = trading::config::run_profile_startup(root, "bad", false, true, ctx);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, trading::config::ConfigErrorCode::UnknownKey);
}

TEST_F(ProfileFixture, TCCF08GrowwDisabledFails) {
  std::string body = kValidProfileBody;
  body.replace(body.find("adapter = \"mock\""), std::string("adapter = \"mock\"").size(),
             "adapter = \"groww\"");
  write_profile("groww", body);
  trading::config::StartupContext ctx;
  const auto err = trading::config::run_profile_startup(root, "groww", false, true, ctx);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, trading::config::ConfigErrorCode::GrowwAdapterDisabled);
}

TEST_F(ProfileFixture, TCCF09MissingSecretFailsWithoutValue) {
  unsetenv("TRADING_SECRET_TRADING_PG_PROJECTOR");
  write_profile("paper", kValidProfileBody);
  trading::config::StartupContext ctx;
  const auto err = trading::config::run_profile_startup(root, "paper", false, true, ctx);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, trading::config::ConfigErrorCode::SecretMissing);
  EXPECT_EQ(err->message.find("test-secret"), std::string::npos);
}

TEST_F(ProfileFixture, TCCF07ConfigHashComputed) {
  write_profile("paper", kValidProfileBody);
  trading::config::StartupContext ctx;
  ASSERT_FALSE(trading::config::run_profile_startup(root, "paper", false, true, ctx));
  EXPECT_EQ(ctx.config_hash_hex.size(), 64U);
}

TEST_F(ProfileFixture, TCCF03LiveWithoutPromotionFileFails) {
  std::string body = kValidProfileBody;
  body.replace(body.find("mode = \"paper\""), std::string("mode = \"paper\"").size(),
             "mode = \"live\"");
  body += R"(
[promotion]
file = "missing/promotion.json"
public_key_pem = "-----BEGIN PUBLIC KEY-----\nMCowBQYDK2VwAyEA\n-----END PUBLIC KEY-----\n"
)";
  write_profile("live", body);
  trading::config::StartupContext ctx;
  const auto err = trading::config::run_profile_startup(root, "live", false, true, ctx);
  ASSERT_TRUE(err.has_value());
  EXPECT_TRUE(err->code == trading::config::ConfigErrorCode::PromotionMissing ||
              err->code == trading::config::ConfigErrorCode::PromotionInvalid);
}

TEST_F(ProfileFixture, TCCF04WrongConfigHashFails) {
  auto keys = generate_ed25519_keypair();
  std::string profile_body = R"(
[profile]
name = "live"
mode = "live"
engine_build_required = ">=0.1.0"
[execution]
adapter = "mock"
client_id_prefix_source = "counter"
[market_data]
instruments_file = "config/instruments/delta_crypto.toml"
calendar = "config/calendars/crypto_24x7.toml"
stale_threshold_ms = 3000
[journal]
root = "data/journal"
segment_max_mb = 256
group_commit_ms = 2
[risk]
limits_source = "compiled"
tighten_overrides_file = "config/risk/overrides.toml"
[database]
url_secret = "trading/pg/projector"
[control]
socket = "/tmp/trading/control.sock"
[promotion]
file = "promotion/good.json"
public_key_pem = )";
  profile_body += escape_for_toml(keys.public_pem);
  write_profile("live", profile_body);

  trading::config::StartupContext ctx;
  fs::create_directories(root / "promotion");
  nlohmann::json promo{
      {"format_version", 1},
      {"release_tag", "v0.1.0"},
      {"engine_build_hash", std::string(64, 'a')},
      {"config_hash", std::string(64, 'b')},
      {"paper_report_hash", std::string(64, 'c')},
      {"approver", "operator"},
      {"approved_at", "2026-01-01T00:00:00"},
      {"expires_at", "2030-01-01T00:00:00"},
      {"signature", "AAAA"},
  };
  std::ofstream(root / "promotion/good.json") << promo.dump();

  const auto err = trading::config::run_profile_startup(root, "live", false, true, ctx);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, trading::config::ConfigErrorCode::PromotionHashMismatch);
  EVP_PKEY_free(keys.private_key);
}

TEST_F(ProfileFixture, TCCF05BadSignatureFails) {
  auto keys = generate_ed25519_keypair();
  std::string profile_body = R"(
[profile]
name = "live"
mode = "live"
engine_build_required = ">=0.1.0"
[execution]
adapter = "mock"
client_id_prefix_source = "counter"
[market_data]
instruments_file = "config/instruments/delta_crypto.toml"
calendar = "config/calendars/crypto_24x7.toml"
stale_threshold_ms = 3000
[journal]
root = "data/journal"
segment_max_mb = 256
group_commit_ms = 2
[risk]
limits_source = "compiled"
tighten_overrides_file = "config/risk/overrides.toml"
[database]
url_secret = "trading/pg/projector"
[control]
socket = "/tmp/trading/control.sock"
[promotion]
file = "promotion/bad-sig.json"
public_key_pem = )";
  profile_body += escape_for_toml(keys.public_pem);
  write_profile("live", profile_body);

  trading::config::StartupContext ctx;
  ASSERT_TRUE(trading::config::run_profile_startup(root, "live", false, true, ctx).has_value());

  fs::create_directories(root / "promotion");
  nlohmann::json promo{
      {"format_version", 1},
      {"release_tag", "v0.1.0"},
      {"engine_build_hash", std::string(64, 'a')},
      {"config_hash", ctx.config_hash_hex},
      {"paper_report_hash", std::string(64, 'c')},
      {"approver", "operator"},
      {"approved_at", "2026-01-01T00:00:00"},
      {"expires_at", "2030-01-01T00:00:00"},
      {"signature", "AAAA"},
  };
  std::ofstream(root / "promotion/bad-sig.json") << promo.dump();

  const auto err = trading::config::run_profile_startup(root, "live", false, true, ctx);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, trading::config::ConfigErrorCode::PromotionSignatureInvalid);
  EVP_PKEY_free(keys.private_key);
}

TEST_F(ProfileFixture, TCCF06ExpiredPromotionFails) {
  auto keys = generate_ed25519_keypair();
  std::string profile_body = R"(
[profile]
name = "live"
mode = "live"
engine_build_required = ">=0.1.0"
[execution]
adapter = "mock"
client_id_prefix_source = "counter"
[market_data]
instruments_file = "config/instruments/delta_crypto.toml"
calendar = "config/calendars/crypto_24x7.toml"
stale_threshold_ms = 3000
[journal]
root = "data/journal"
segment_max_mb = 256
group_commit_ms = 2
[risk]
limits_source = "compiled"
tighten_overrides_file = "config/risk/overrides.toml"
[database]
url_secret = "trading/pg/projector"
[control]
socket = "/tmp/trading/control.sock"
[promotion]
file = "promotion/expired.json"
public_key_pem = )";
  profile_body += escape_for_toml(keys.public_pem);
  write_profile("live", profile_body);

  trading::config::StartupContext ctx;
  ASSERT_TRUE(trading::config::run_profile_startup(root, "live", false, true, ctx).has_value());

  nlohmann::json promo{
      {"format_version", 1},
      {"release_tag", "v0.1.0"},
      {"engine_build_hash", std::string(64, 'a')},
      {"config_hash", ctx.config_hash_hex},
      {"paper_report_hash", std::string(64, 'c')},
      {"approver", "operator"},
      {"approved_at", "2020-01-01T00:00:00"},
      {"expires_at", "2020-01-02T00:00:00"},
      {"signature", "AAAA"},
  };
  const std::string payload = trading::config::promotion_signing_payload(promo);
  promo["signature"] = sign_payload(keys.private_key, payload);
  fs::create_directories(root / "promotion");
  std::ofstream(root / "promotion/expired.json") << promo.dump();

  const auto err = trading::config::run_profile_startup(root, "live", false, true, ctx);
  ASSERT_TRUE(err.has_value());
  EXPECT_EQ(err->code, trading::config::ConfigErrorCode::PromotionExpired);
  EVP_PKEY_free(keys.private_key);
}

TEST_F(ProfileFixture, ValidSignedPromotionPasses) {
  auto keys = generate_ed25519_keypair();
  std::string profile_body = R"(
[profile]
name = "live"
mode = "live"
engine_build_required = ">=0.1.0"
[execution]
adapter = "mock"
client_id_prefix_source = "counter"
[market_data]
instruments_file = "config/instruments/delta_crypto.toml"
calendar = "config/calendars/crypto_24x7.toml"
stale_threshold_ms = 3000
[journal]
root = "data/journal"
segment_max_mb = 256
group_commit_ms = 2
[risk]
limits_source = "compiled"
tighten_overrides_file = "config/risk/overrides.toml"
[database]
url_secret = "trading/pg/projector"
[control]
socket = "/tmp/trading/control.sock"
[promotion]
file = "promotion/valid.json"
public_key_pem = )";
  profile_body += escape_for_toml(keys.public_pem);
  write_profile("live", profile_body);

  trading::config::StartupContext ctx;
  ASSERT_TRUE(trading::config::run_profile_startup(root, "live", false, true, ctx).has_value());

  nlohmann::json promo{
      {"format_version", 1},
      {"release_tag", "v0.1.0"},
      {"engine_build_hash", std::string(64, 'a')},
      {"config_hash", ctx.config_hash_hex},
      {"paper_report_hash", std::string(64, 'c')},
      {"approver", "operator"},
      {"approved_at", "2026-01-01T00:00:00"},
      {"expires_at", "2030-01-01T00:00:00"},
      {"signature", "AAAA"},
  };
  const std::string payload = trading::config::promotion_signing_payload(promo);
  promo["signature"] = sign_payload(keys.private_key, payload);
  fs::create_directories(root / "promotion");
  std::ofstream(root / "promotion/valid.json") << promo.dump();

  EXPECT_FALSE(trading::config::run_profile_startup(root, "live", false, true, ctx));
  EVP_PKEY_free(keys.private_key);
}
