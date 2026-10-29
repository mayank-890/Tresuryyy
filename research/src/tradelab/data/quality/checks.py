"""Gap detection and basic outlier flags for OHLCV series (T-0101)."""

from __future__ import annotations

from dataclasses import dataclass

from tradelab.data.ingest.delta_rest import OhlcvBar, resolution_to_seconds


@dataclass(frozen=True, slots=True)
class Gap:
    after_open_time_ns: int
    missing_open_time_ns: int
    resolution: str


@dataclass(frozen=True, slots=True)
class OutlierFlag:
    open_time_ns: int
    reason: str


@dataclass(frozen=True, slots=True)
class QualityReport:
    gaps: tuple[Gap, ...]
    outliers: tuple[OutlierFlag, ...]

    @property
    def gap_count(self) -> int:
        return len(self.gaps)

    @property
    def outlier_count(self) -> int:
        return len(self.outliers)


def detect_gaps(bars: list[OhlcvBar]) -> list[Gap]:
    if len(bars) < 2:
        return []
    resolution = bars[0].resolution
    step_ns = resolution_to_seconds(resolution) * 1_000_000_000
    gaps: list[Gap] = []
    for idx in range(1, len(bars)):
        prev = bars[idx - 1]
        curr = bars[idx]
        expected = prev.open_time_ns + step_ns
        if curr.open_time_ns > expected:
            missing = expected
            while missing < curr.open_time_ns:
                gaps.append(
                    Gap(
                        after_open_time_ns=prev.open_time_ns,
                        missing_open_time_ns=missing,
                        resolution=resolution,
                    )
                )
                missing += step_ns
    return gaps


def flag_outliers(
    bars: list[OhlcvBar],
    *,
    max_abs_return: float = 0.5,
) -> list[OutlierFlag]:
    """Flag structural errors and large bar-to-bar returns (research threshold, not production)."""
    flags: list[OutlierFlag] = []
    prev_close: float | None = None
    for bar in bars:
        if bar.high < bar.low:
            flags.append(OutlierFlag(bar.open_time_ns, "high_below_low"))
        if bar.open < bar.low or bar.open > bar.high:
            flags.append(OutlierFlag(bar.open_time_ns, "open_outside_range"))
        if bar.close < bar.low or bar.close > bar.high:
            flags.append(OutlierFlag(bar.open_time_ns, "close_outside_range"))
        if bar.volume < 0:
            flags.append(OutlierFlag(bar.open_time_ns, "negative_volume"))
        if prev_close is not None and prev_close > 0:
            ret = abs(bar.close - prev_close) / prev_close
            if ret > max_abs_return:
                flags.append(OutlierFlag(bar.open_time_ns, "large_return"))
        prev_close = bar.close
    return flags


def run_quality_checks(
    bars: list[OhlcvBar],
    *,
    max_abs_return: float = 0.5,
) -> QualityReport:
    sorted_bars = sorted(bars, key=lambda b: b.open_time_ns)
    return QualityReport(
        gaps=tuple(detect_gaps(sorted_bars)),
        outliers=tuple(flag_outliers(sorted_bars, max_abs_return=max_abs_return)),
    )
