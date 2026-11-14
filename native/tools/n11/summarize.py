#!/usr/bin/env python3
"""Summarize Cat Arena gameplay captures from --benchmark-csv."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path


def samples(rows, field):
    return [float(row[field]) for row in rows if row[field]]


def sample_peak(rows, field):
    values = [int(row[field]) for row in rows if row[field]]
    return max(values, default=None)


def low_fps(descending, fraction, minimum_frames):
    if len(descending) < minimum_frames:
        return None
    count = math.ceil(len(descending) * fraction)
    return 1000 / statistics.mean(descending[:count])


def summarize(rows, warmup_seconds):
    # The first frame has no prior submission. Trim by elapsed time without
    # dropping later long frames. The first retained interval defines the start.
    elapsed = 0.0
    kept = []
    for row in rows:
        if not row["frame_ms"]:
            continue
        frame_ms = float(row["frame_ms"])
        if frame_ms <= 0 or not math.isfinite(frame_ms):
            raise ValueError("frame intervals must be finite and positive")
        if elapsed >= warmup_seconds * 1000:
            kept.append(row)
        elapsed += frame_ms
    frame_ms = sorted(samples(kept, "frame_ms"), reverse=True)
    cpu = samples(kept, "cpu_ms")
    # The GPU result appears on the next submitted row. Exclude the first row's
    # result because its GPU frame precedes the retained interval.
    gpu = samples(kept[1:], "previous_gpu_ms")
    first = rows[0]
    return {
        "segment": int(first["segment"]),
        "map": first["map"],
        "mode": first["mode"],
        "width": int(first["width"]),
        "height": int(first["height"]),
        "present_mode": first["present_mode"],
        "warmup_seconds": warmup_seconds,
        "measured_seconds": sum(frame_ms) / 1000,
        "frame_intervals": len(frame_ms),
        "average_fps": 1000 / statistics.mean(frame_ms) if frame_ms else None,
        "low_1_percent_fps": low_fps(frame_ms, .01, 100),
        "low_0_1_percent_fps": low_fps(frame_ms, .001, 1000),
        "cpu_mean_ms": statistics.mean(cpu) if cpu else None,
        "gpu_mean_ms": statistics.mean(gpu) if gpu else None,
        "gpu_samples": len(gpu),
        "frames_over_25_ms": sum(ms >= 25 for ms in frame_ms),
        "mean_draw_calls": statistics.mean(samples(kept, "draw_calls")) if kept else None,
        "mean_submitted_triangles": statistics.mean(samples(kept, "submitted_triangles")) if kept else None,
        "peak_active_enemies": sample_peak(kept, "enemies"),
        "peak_vulkan_allocated_bytes": sample_peak(kept, "vulkan_allocated_bytes"),
        "peak_sampled_working_set_bytes": sample_peak(kept, "working_set_bytes"),
        "process_lifetime_peak_working_set_bytes": sample_peak(kept, "peak_working_set_bytes"),
        "peak_sampled_private_commit_bytes": sample_peak(kept, "private_commit_bytes"),
        "process_memory_samples": sum(bool(row["working_set_bytes"]) for row in kept),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--warmup-seconds", type=float, default=0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.warmup_seconds < 0 or not math.isfinite(args.warmup_seconds):
        parser.error("warmup must be a finite nonnegative number")
    with args.capture.open(newline="", encoding="utf-8") as source:
        segments = {}
        for row in csv.DictReader(source):
            segments.setdefault(row["segment"], []).append(row)
    if not segments:
        parser.error("capture has no gameplay frames")
    result = [summarize(rows, args.warmup_seconds) for rows in segments.values()]
    text = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
