#!/usr/bin/env python3
"""Summarize local AnyPS5 display-completion telemetry; never infer FPS from submissions.

The histogram is cumulative within a foreground/swapchain epoch. Subtracting
endpoints avoids double-counting the rolling 256-frame percentile windows.
Percentiles are conservative 0.1-ms bucket upper bounds. The last bucket is
explicitly unbounded (>25.6 ms), rather than a falsely precise value.
"""
import argparse
import json
import math
from pathlib import Path

PREFIX = "[anyps5-display] "
SOURCE = "VK_GOOGLE_display_timing"
BINS = 257
STEP_NS = 100000


def read_rows(text):
    rows = []
    for number, line in enumerate(text.splitlines(), 1):
        if PREFIX not in line:
            continue
        try:
            row = json.loads(line.split(PREFIX, 1)[1])
        except (ValueError, TypeError) as error:
            raise ValueError(f"Malformed display telemetry at line {number}") from error
        if row.get("schema") != 1 or row.get("source") != SOURCE:
            raise ValueError(f"Unsupported display telemetry at line {number}")
        names = ("monotonic_ns", "actual_ns", "swapchain_displayed_total", "epoch", "swapchain_id", "width", "height", "refresh_ns", "missed_vblanks", "active")
        for name in names:
            if type(row.get(name)) is not int or row[name] < 0:
                raise ValueError(f"Invalid {name} at line {number}")
        histogram = row.get("interval_histogram")
        if row.get("histogram_step_ns") != STEP_NS or not isinstance(histogram, list) or len(histogram) != BINS or any(type(n) is not int or n < 0 for n in histogram):
            raise ValueError(f"Invalid interval histogram at line {number}")
        rows.append(row)
    return rows


def percentile(histogram, fraction):
    total = sum(histogram)
    if not total:
        return None
    target, count = math.ceil(total * fraction), 0
    for i, value in enumerate(histogram):
        count += value
        if count >= target:
            return {"upper_ms": (i + 1) * STEP_NS / 1e6 if i < BINS - 1 else None,
                    "over_ms": (BINS - 1) * STEP_NS / 1e6 if i == BINS - 1 else None}
    raise AssertionError("unreachable percentile")


def summarize(rows, warmup_seconds=10.0, minimum_seconds=60.0):
    if warmup_seconds < 0 or minimum_seconds < 0:
        raise ValueError("Durations must be nonnegative")
    groups = {}
    for row in rows:
        if row["active"] != 1 or row["actual_ns"] == 0:
            continue
        key = (row["epoch"], row["swapchain_id"], row["width"], row["height"])
        groups.setdefault(key, []).append(row)
    segments = []
    for key, values in groups.items():
        # File order matters: sorting could conceal clock or count regressions.
        previous = None
        for row in values:
            if previous:
                if row["monotonic_ns"] <= previous["monotonic_ns"] or row["actual_ns"] < previous["actual_ns"] or row["swapchain_displayed_total"] < previous["swapchain_displayed_total"]:
                    raise ValueError("Display timestamp/counter regression within one epoch")
                if any(b < a for a, b in zip(previous["interval_histogram"], row["interval_histogram"])):
                    raise ValueError("Interval histogram regressed within one epoch")
            previous = row
        cutoff = values[0]["monotonic_ns"] + int(warmup_seconds * 1e9)
        kept = [v for v in values if v["monotonic_ns"] >= cutoff]
        if len(kept) < 2:
            continue
        first, last = kept[0], kept[-1]
        elapsed_ns = last["actual_ns"] - first["actual_ns"]
        if elapsed_ns <= 0:
            continue
        histogram = [b - a for a, b in zip(first["interval_histogram"], last["interval_histogram"])]
        frames = last["swapchain_displayed_total"] - first["swapchain_displayed_total"]
        if sum(histogram) != frames:
            raise ValueError("Displayed count does not match frame-interval distribution")
        seconds = elapsed_ns / 1e9
        segments.append({"epoch": key[0], "swapchain_id": key[1], "resolution": [key[2], key[3]],
                         "seconds": seconds, "displayed_intervals": frames, "displayed_fps": frames / seconds,
                         "p50": percentile(histogram, 0.5), "p95": percentile(histogram, 0.95), "p99": percentile(histogram, 0.99),
                         "missed_vblanks": last["missed_vblanks"] - first["missed_vblanks"],
                         "refresh_hz": 1e9 / last["refresh_ns"] if last["refresh_ns"] else None,
                         "minimum_duration_met": seconds >= minimum_seconds})
    return {"schema": 1, "source": SOURCE, "status": "measured" if segments else "insufficient_display_timing",
            "warmup_seconds_per_epoch": warmup_seconds, "minimum_seconds": minimum_seconds, "segments": segments,
            "limits": ["Display-completion timing requires separate visible foreground verification.",
                       "No claim about guest simulation speed, visual correctness, touch, thermal stability or device acceptance.",
                       "Percentiles are conservative 0.1 ms histogram upper bounds; overflow is explicitly unbounded."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--warmup-seconds", type=float, default=10)
    parser.add_argument("--minimum-seconds", type=float, default=60)
    args = parser.parse_args()
    try:
        result = summarize(read_rows(args.log.read_text(errors="replace")), args.warmup_seconds, args.minimum_seconds)
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
