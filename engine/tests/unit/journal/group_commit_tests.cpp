#include "trading/journal/generated/records.hpp"
#include "trading/journal/group_commit.hpp"
#include "trading/journal/recovery.hpp"
#include "trading/journal/writer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using trading::journal::GroupCommitConfig;
using trading::journal::GroupCommitJournal;
using trading::journal::GroupError;
using trading::journal::IoHooks;
using trading::journal::RecoveryApply;
using trading::journal::ScanKind;
using trading::journal::ScanReason;
using trading::journal::kAlertAl14;
using trading::journal::kGroupCommitWindowNs;
using trading::journal::plan_segment;
using trading::journal::recover_segment;
using trading::journal::v1::RecordType;
using trading::journal::encode_record;
using trading::journal::v1::kRecordHeaderSize;

std::vector<std::byte> payload_of(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size());
  for (const char ch : text) {
    out.push_back(static_cast<std::byte>(ch));
  }
  return out;
}

std::filesystem::path scratch(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "trading_t0303" / name;
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  return dir;
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    out[i] = static_cast<std::byte>(raw[i]);
  }
  return out;
}

struct Probe {
  int writes{0};
  int fsyncs{0};
  int fsync_attempts{0};
  bool fail_write{false};
  bool fail_fsync{false};
  std::uint64_t durable_during_fsync{99};
  GroupCommitJournal* journal{nullptr};
  std::vector<std::byte> data;
};

bool write_hook(void* ctx, const std::byte* data, std::size_t size) {
  auto* probe = static_cast<Probe*>(ctx);
  if (probe->fail_write) {
    return false;
  }
  probe->data.insert(probe->data.end(), data, data + size);
  ++probe->writes;
  return true;
}

bool fsync_hook(void* ctx) {
  auto* probe = static_cast<Probe*>(ctx);
  ++probe->fsync_attempts;
  if (probe->journal != nullptr) {
    probe->durable_during_fsync = probe->journal->durable_sequence();
  }
  if (probe->fail_fsync) {
    return false;
  }
  ++probe->fsyncs;
  return true;
}

IoHooks hooks_of(Probe* probe) {
  IoHooks hooks;
  hooks.ctx = probe;
  hooks.write = write_hook;
  hooks.fsync = fsync_hook;
  return hooks;
}

GroupCommitConfig test_config() {
  GroupCommitConfig config;
  config.window_ns = kGroupCommitWindowNs;
  config.batch_capacity = 64 * 1024;
  config.size_threshold = 0;
  return config;
}

} // namespace

TEST(GroupCommit, AppendMatchesEncoderAndPublishesAfterFsync) {
  Probe probe;
  GroupCommitJournal journal(hooks_of(&probe), test_config());
  probe.journal = &journal;
  ASSERT_TRUE(journal.open().is_ok());
  const auto payload = payload_of("intent-1");
  const auto first = journal.append(RecordType::OrderIntent, payload, 10, 20);
  const auto second = journal.append(RecordType::OrderIntent, payload, 11, 21);
  ASSERT_TRUE(first.is_ok());
  ASSERT_TRUE(second.is_ok());
  EXPECT_EQ(first.value(), 1U);
  EXPECT_EQ(second.value(), 2U);
  EXPECT_EQ(journal.durable_sequence(), 0U);
  EXPECT_EQ(probe.fsyncs, 0);
  EXPECT_FALSE(journal.send_allowed(first.value()));
  EXPECT_FALSE(journal.send_allowed(second.value()));

  ASSERT_TRUE(journal.commit().is_ok());
  EXPECT_EQ(probe.fsync_attempts, 1);
  EXPECT_EQ(probe.durable_during_fsync, 0U);
  EXPECT_EQ(journal.durable_sequence(), 2U);
  EXPECT_TRUE(journal.send_allowed(first.value()));
  EXPECT_TRUE(journal.send_allowed(second.value()));
  EXPECT_FALSE(journal.send_allowed(3));

  const auto expected_first = encode_record(RecordType::OrderIntent, payload, 1, 10, 20);
  const auto expected_second = encode_record(RecordType::OrderIntent, payload, 2, 11, 21);
  ASSERT_EQ(probe.data.size(), expected_first.size() + expected_second.size());
  EXPECT_TRUE(std::equal(expected_first.begin(), expected_first.end(), probe.data.begin()));
  EXPECT_TRUE(std::equal(expected_second.begin(), expected_second.end(),
                         probe.data.begin() + static_cast<std::ptrdiff_t>(expected_first.size())));
}

TEST(GroupCommit, WindowAndSizeThreshold) {
  GroupCommitConfig config = test_config();
  Probe probe;
  GroupCommitJournal journal(hooks_of(&probe), config);
  ASSERT_TRUE(journal.open().is_ok());
  const auto payload = payload_of("q");
  ASSERT_TRUE(journal.append(RecordType::MarketQuote, payload, 1'000, 1).is_ok());
  ASSERT_TRUE(journal.maybe_commit(1'000 + kGroupCommitWindowNs - 1).is_ok());
  EXPECT_EQ(probe.fsyncs, 0);
  EXPECT_EQ(journal.durable_sequence(), 0U);
  ASSERT_TRUE(journal.maybe_commit(1'000 + kGroupCommitWindowNs).is_ok());
  EXPECT_EQ(probe.fsyncs, 1);
  EXPECT_EQ(journal.durable_sequence(), 1U);

  Probe sized;
  config.size_threshold = kRecordHeaderSize + payload.size();
  GroupCommitJournal by_size(hooks_of(&sized), config);
  ASSERT_TRUE(by_size.open().is_ok());
  ASSERT_TRUE(by_size.append(RecordType::MarketQuote, payload, 5, 5).is_ok());
  ASSERT_TRUE(by_size.maybe_commit(5).is_ok());
  EXPECT_EQ(sized.fsyncs, 1);
  EXPECT_EQ(by_size.durable_sequence(), 1U);
}

TEST(GroupCommit, FsyncFailureDoesNotPublishAndDiskFullHalts) {
  Probe probe;
  probe.fail_fsync = true;
  GroupCommitJournal journal(hooks_of(&probe), test_config());
  probe.journal = &journal;
  ASSERT_TRUE(journal.open().is_ok());
  const auto payload = payload_of("keep");
  const auto seq = journal.append(RecordType::OrderIntent, payload, 1, 1);
  ASSERT_TRUE(seq.is_ok());
  EXPECT_EQ(journal.commit().error(), GroupError::IoError);
  EXPECT_EQ(probe.durable_during_fsync, 0U);
  EXPECT_EQ(journal.durable_sequence(), 0U);
  EXPECT_FALSE(journal.send_allowed(seq.value()));
  EXPECT_FALSE(journal.halted());
  probe.fail_fsync = false;
  ASSERT_TRUE(journal.commit().is_ok());
  EXPECT_EQ(journal.durable_sequence(), seq.value());
  EXPECT_TRUE(journal.send_allowed(seq.value()));

  Probe full;
  full.fail_write = true;
  GroupCommitJournal blocked(hooks_of(&full), test_config());
  ASSERT_TRUE(blocked.open().is_ok());
  ASSERT_TRUE(blocked.append(RecordType::OrderIntent, payload, 2, 2).is_ok());
  EXPECT_EQ(blocked.commit().error(), GroupError::IoError);
  EXPECT_TRUE(blocked.halted());
  EXPECT_EQ(blocked.durable_sequence(), 0U);
  EXPECT_FALSE(blocked.send_allowed(1));
  EXPECT_EQ(blocked.append(RecordType::OrderIntent, payload, 3, 3).error(), GroupError::Halted);
  EXPECT_EQ(full.fsyncs, 0);
}

TEST(GroupCommit, OrderIntentIsDurableBeforeSend) {
  const auto dir = scratch("intent");
  GroupCommitJournal journal(dir / "segment.journal", test_config());
  const auto opened = journal.open();
  ASSERT_TRUE(opened.is_ok());
  EXPECT_FALSE(opened.value().alert_al14);
  trading::journal::v1::OrderIntentPayload intent{};
  intent.client_id[0] = 'C';
  intent.strategy_id = 7;
  intent.instrument_id = 11;
  intent.side = trading::journal::v1::Side::Buy;
  intent.qty_lots = 1;
  intent.price_ticks = 100;
  intent.time_in_force = trading::journal::v1::TimeInForce::Day;
  const auto encoded = trading::journal::v1::encode_order_intent(intent);
  const auto seq = journal.append(RecordType::OrderIntent, encoded, 50, 60);
  ASSERT_TRUE(seq.is_ok());
  EXPECT_FALSE(journal.send_allowed(seq.value()));
  ASSERT_TRUE(journal.commit().is_ok());
  EXPECT_TRUE(journal.send_allowed(seq.value()));
  journal.close();

  const auto file = read_file(dir / "segment.journal");
  const auto expected = encode_record(RecordType::OrderIntent, encoded, seq.value(), 50, 60);
  EXPECT_EQ(file, expected);
  trading::journal::v1::RecordHeader header{};
  ASSERT_TRUE(trading::journal::v1::decode_header(file, header));
  EXPECT_EQ(header.sequence, seq.value());
  EXPECT_EQ(header.crc32c, static_cast<std::uint32_t>(expected[36]) |
                               (static_cast<std::uint32_t>(expected[37]) << 8) |
                               (static_cast<std::uint32_t>(expected[38]) << 16) |
                               (static_cast<std::uint32_t>(expected[39]) << 24));
}

TEST(GroupCommit, TornTailRaisesAl14AndKeepsPrefix) {
  const auto dir = scratch("torn");
  const auto path = dir / "segment.journal";
  std::vector<std::byte> prefix;
  {
    GroupCommitJournal journal(path, test_config());
    ASSERT_TRUE(journal.open().is_ok());
    for (int i = 0; i < 3; ++i) {
      const auto payload = payload_of("rec");
      ASSERT_TRUE(journal.append(RecordType::MarketTrade, payload, static_cast<std::uint64_t>(i), 1)
                      .is_ok());
    }
    ASSERT_TRUE(journal.commit().is_ok());
    journal.close();
    prefix = read_file(path);
  }
  {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    const char junk[] = {'\x01', '\x02', '\x03', '\x04', '\x05'};
    out.write(junk, sizeof(junk));
  }
  const auto torn = read_file(path);
  ASSERT_GT(torn.size(), prefix.size());
  const auto dry = recover_segment(path, false);
  EXPECT_EQ(dry.action, RecoveryApply::DryRun);
  EXPECT_EQ(dry.plan.kind, ScanKind::TornTail);
  EXPECT_EQ(dry.plan.reason, ScanReason::IncompleteHeader);
  EXPECT_TRUE(dry.plan.alert_al14);
  EXPECT_EQ(kAlertAl14, 14);
  EXPECT_EQ(read_file(path), torn);

  GroupCommitJournal recovered(path, test_config());
  const auto opened = recovered.open();
  ASSERT_TRUE(opened.is_ok());
  EXPECT_TRUE(opened.value().alert_al14);
  EXPECT_EQ(opened.value().recovery, RecoveryApply::Applied);
  EXPECT_EQ(opened.value().records_kept, 3U);
  EXPECT_EQ(read_file(path), prefix);
  const auto backup = read_file(dir / "segment.journal.torn");
  EXPECT_EQ(backup, torn);
  const auto next = recovered.append(RecordType::MarketTrade, payload_of("rec"), 4, 1);
  ASSERT_TRUE(next.is_ok());
  EXPECT_EQ(next.value(), 4U);
  EXPECT_FALSE(recovered.send_allowed(next.value()));
  ASSERT_TRUE(recovered.commit().is_ok());
  EXPECT_TRUE(recovered.send_allowed(4));
  recovered.close();
  EXPECT_EQ(plan_segment(read_file(path)).kind, ScanKind::Ok);
  EXPECT_EQ(plan_segment(read_file(path)).records, 4U);
}

TEST(GroupCommit, OneHundredTornTailsRecoverTheSamePrefix) {
  const auto dir = scratch("hundred");
  const auto payload = payload_of("stable");
  std::vector<std::byte> prefix;
  {
    GroupCommitJournal journal(dir / "base.journal", test_config());
    ASSERT_TRUE(journal.open().is_ok());
    ASSERT_TRUE(journal.append(RecordType::MarketTrade, payload, 1, 1).is_ok());
    ASSERT_TRUE(journal.append(RecordType::MarketTrade, payload, 2, 2).is_ok());
    ASSERT_TRUE(journal.commit().is_ok());
    journal.close();
    prefix = read_file(dir / "base.journal");
  }
  const auto long_payload = payload_of("0123456789abcdef0123456789abcdef");
  const auto long_record = encode_record(RecordType::MarketTrade, long_payload, 3, 3, 3);
  for (int run = 1; run <= 100; ++run) {
    const auto path = dir / ("torn-" + std::to_string(run) + ".journal");
    {
      std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(prefix.data()),
                static_cast<std::streamsize>(prefix.size()));
      if (run <= 50) {
        const std::string junk(static_cast<std::size_t>((run % 39) + 1), '\x7f');
        out.write(junk.data(), static_cast<std::streamsize>(junk.size()));
      } else {
        const std::size_t keep = kRecordHeaderSize + static_cast<std::size_t>(run % 20);
        out.write(reinterpret_cast<const char*>(long_record.data()),
                  static_cast<std::streamsize>(keep));
      }
    }
    const auto expected_reason =
        run <= 50 ? ScanReason::IncompleteHeader : ScanReason::IncompletePayload;
    EXPECT_EQ(plan_segment(read_file(path)).reason, expected_reason) << run;
    GroupCommitJournal journal(path, test_config());
    const auto opened = journal.open();
    ASSERT_TRUE(opened.is_ok()) << run;
    EXPECT_TRUE(opened.value().alert_al14) << run;
    EXPECT_EQ(opened.value().records_kept, 2U) << run;
    EXPECT_EQ(read_file(path), prefix) << run;
    journal.close();
  }
}

TEST(GroupCommit, CrcAndSequenceGapAreNotTruncated) {
  const auto dir = scratch("fatal");
  const auto payload = payload_of("abcd");
  const auto first = encode_record(RecordType::MarketQuote, payload, 1, 1, 1);
  const auto second = encode_record(RecordType::MarketQuote, payload, 2, 2, 2);
  const auto third = encode_record(RecordType::MarketQuote, payload, 3, 3, 3);
  std::vector<std::byte> intact = first;
  intact.insert(intact.end(), second.begin(), second.end());
  intact.insert(intact.end(), third.begin(), third.end());
  const auto crc_path = dir / "crc.journal";
  {
    std::ofstream out(crc_path, std::ios::binary);
    auto damaged = intact;
    damaged[first.size() + kRecordHeaderSize] = std::byte{0x5a};
    out.write(reinterpret_cast<const char*>(damaged.data()), static_cast<std::streamsize>(damaged.size()));
  }
  const auto crc_before = read_file(crc_path);
  EXPECT_EQ(plan_segment(crc_before).kind, ScanKind::Fatal);
  EXPECT_EQ(plan_segment(crc_before).reason, ScanReason::CrcMismatch);
  GroupCommitJournal crc_journal(crc_path, test_config());
  EXPECT_EQ(crc_journal.open().error(), GroupError::FatalCorruption);
  EXPECT_EQ(read_file(crc_path), crc_before);
  EXPECT_FALSE(std::filesystem::exists(dir / "crc.journal.torn"));

  const auto gap = encode_record(RecordType::MarketQuote, payload, 4, 4, 4);
  std::vector<std::byte> gapped = first;
  gapped.insert(gapped.end(), gap.begin(), gap.end());
  const auto gap_path = dir / "gap.journal";
  {
    std::ofstream out(gap_path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(gapped.data()), static_cast<std::streamsize>(gapped.size()));
  }
  const auto gap_before = read_file(gap_path);
  EXPECT_EQ(plan_segment(gap_before).reason, ScanReason::SequenceGap);
  GroupCommitJournal gap_journal(gap_path, test_config());
  EXPECT_EQ(gap_journal.open().error(), GroupError::FatalCorruption);
  EXPECT_EQ(read_file(gap_path), gap_before);
  EXPECT_FALSE(std::filesystem::exists(dir / "gap.journal.torn"));
}

TEST(GroupCommit, ClosedFooterIsLeftInPlace) {
  const auto dir = scratch("footer");
  const auto path = dir / "closed.journal";
  trading::journal::SegmentWriter writer(path);
  ASSERT_TRUE(writer.open().is_ok());
  const auto payload = payload_of("body");
  ASSERT_TRUE(writer.append(RecordType::MarketQuote, payload, 1, 2).is_ok());
  ASSERT_TRUE(writer.flush().is_ok());
  ASSERT_TRUE(writer.finalize_segment(1, 1).is_ok());
  const auto before = read_file(path);
  const auto planned = recover_segment(path, true);
  EXPECT_EQ(planned.action, RecoveryApply::None);
  EXPECT_TRUE(planned.plan.closed_segment);
  EXPECT_FALSE(planned.plan.alert_al14);
  EXPECT_EQ(read_file(path), before);
  GroupCommitJournal journal(path, test_config());
  EXPECT_EQ(journal.open().error(), GroupError::ClosedSegment);
  EXPECT_EQ(read_file(path), before);
}

TEST(GroupCommit, AppendLatencyExcludesFsync) {
  Probe probe;
  GroupCommitConfig config = test_config();
  config.batch_capacity = 2 * 1024 * 1024;
  GroupCommitJournal journal(hooks_of(&probe), config);
  ASSERT_TRUE(journal.open().is_ok());
  const auto payload = payload_of("01234567");
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(journal.append(RecordType::MarketTrade, payload, 1, 1).is_ok());
  }
  constexpr int kSamples = 20000;
  std::vector<std::uint64_t> samples;
  samples.reserve(kSamples);
  for (int i = 0; i < kSamples; ++i) {
    const auto start = std::chrono::steady_clock::now();
    const auto result = journal.append(RecordType::MarketTrade, payload, 1, 1);
    const auto stop = std::chrono::steady_clock::now();
    ASSERT_TRUE(result.is_ok());
    samples.push_back(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()));
  }
  ASSERT_EQ(probe.fsyncs, 0);
  ASSERT_EQ(probe.writes, 0);
  std::sort(samples.begin(), samples.end());
  const std::uint64_t p99 = samples[(samples.size() * 99) / 100];
  EXPECT_LE(p99, 50'000U) << "append p99 ns " << p99;
}
