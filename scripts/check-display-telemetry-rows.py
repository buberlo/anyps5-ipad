#!/usr/bin/env python3
"""Exercise production JSON emission on a regular O_APPEND file, without a GPU.

All counters and Vulkan results here are synthetic format/transport fixtures.
The unchanged display parser must still reject malformed and partial rows.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    root = Path(__file__).resolve().parents[1]
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--output-dir", type=Path, default=root / "build/telemetry-whole-row-20261009/checks")
    out = args.parse_args().output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = root / "tools/checks/display_telemetry_rows.c"
    header = root / "upstreams/Madeira/build/win32u-unix/aps5_display_timing.h"
    parser = root / "tools/perf/summarize_display.py"
    parser_before = digest(parser)
    spec = importlib.util.spec_from_file_location("display", parser)
    display = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(display)
    compiler = shlex.split(os.environ.get("CC", "cc"))
    executable = out / "display-telemetry-rows"
    command = compiler + ["-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-pthread",
                          "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
                          "-I" + str(header.parent), "-I" + str(root / "upstreams/AnyPS5/3rdparty/Vulkan-Headers/include"),
                          str(source), "-o", str(executable)]
    with (out / "compile.log").open("w") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    results = []

    def run(mode, flag="1", suffix=""):
        name = mode + suffix
        path = out / (name + ".log")
        if path.exists():
            raise FileExistsError("Use a fresh --output-dir: " + str(path))
        environment = dict(os.environ)
        environment.pop("APS5_PERF_REPORT", None)
        if flag is not None:
            environment["APS5_PERF_REPORT"] = flag
        environment["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        execution = subprocess.run([str(executable), mode, str(path)], env=environment, check=True,
                                   capture_output=True, text=True, timeout=30)
        assert not execution.stderr, execution.stderr
        result = json.loads(execution.stdout)
        result.update(log_sha256=digest(path), env_flag=flag)
        text = path.read_text()
        results.append(result)
        return text, result

    for flag in (None, "0", "yes", "01", "true", "1"):
        text, result = run("gate", flag, "-" + (flag if flag is not None else "unset"))
        rows = display.read_rows(text)
        native = [json.loads(line.split("] ", 1)[1]) for line in text.splitlines() if line.startswith("[anyps5-native] ")]
        expected = 12 if flag == "1" else 0
        assert len(rows) == len(native) == expected
        assert result["write_calls"] == expected * 2
        if rows:
            assert display.summarize(rows, 0, 0)["status"] == "measured"
        result.update(display_rows=len(rows), native_rows=len(native), exact_one_gate_passed=True)
    for mode, expected in (("max", 1), ("concurrent", 1000)):
        text, result = run(mode)
        rows = display.read_rows(text)
        native = []
        raw_noise = dprintf_noise = 0
        for line in text.splitlines():
            if line.startswith("[anyps5-display] "):
                continue
            if line.startswith("[anyps5-native] "):
                native.append(json.loads(line.split("] ", 1)[1]))
            elif re.fullmatch(r"\[raw-noise\] writer=[0-3] row=\d+", line):
                raw_noise += 1
            elif re.fullmatch(r"\[dprintf-noise\] writer=[0-3] row=\d+", line):
                dprintf_noise += 1
            else:
                raise AssertionError("Unexpected or interleaved fixture line: " + line[:100])
        assert len(rows) == len(native) == expected
        maximum = 2**64 - 1
        assert all(row["interval_histogram"] == [maximum] * 257 for row in rows)
        assert all(row["monotonic_ns"] == row["mean_ns"] == row["p95_ns"] == row["p99_ns"] ==
                   row["actual_ns"] == row["swapchain_displayed_total"] == maximum for row in rows)
        assert all(row["width"] == row["height"] == 2**32 - 1 for row in rows)
        assert all(row[phase + "_" + field] == maximum for row in native
                   for phase in ("submit", "present", "acquire", "idle") for field in ("calls", "ns", "max_ns"))
        assert result["longest_row_bytes"] < result["capacity_bytes"]
        assert (raw_noise, dprintf_noise) == ((4000, 4000) if mode == "concurrent" else (0, 0))
        result.update(display_rows=len(rows), native_rows=len(native), raw_write_noise_rows=raw_noise,
                      separate_append_fd_dprintf_noise_rows=dprintf_noise, full_uint64_bounds_passed=True)
    text, result = run("eintr")
    assert result["write_calls"] == 3
    assert len(text.splitlines()) == 1 and json.loads(text.split("] ", 1)[1])["partial_fixture"] is True
    result["zero_byte_eintr_retries_passed"] = True
    text, result = run("short")
    assert result["write_calls"] == 1 and len(text) == 35
    try:
        display.read_rows(text)
    except ValueError as error:
        assert "Malformed display telemetry" in str(error)
    else:
        raise AssertionError("Strict parser accepted a positive short write")
    result["short_write_not_repaired_strictly_rejected"] = True
    for mode in ("error", "overflow"):
        text, result = run(mode)
        assert not text and result["write_calls"] == (1 if mode == "error" else 0)
        result["no_report_emitted"] = True
    assert digest(parser) == parser_before, "The strict parser changed"
    report = {"schema": 1, "status": "passed", "kind": "synthetic_production_header_format_and_transport_check",
              "source_sha256": digest(source), "production_header_sha256": digest(header), "strict_parser_sha256": parser_before,
              "compiler": subprocess.check_output(compiler + ["--version"], text=True).splitlines()[0],
              "compile_command": command, "compile_log_sha256": digest(out / "compile.log"),
              "executable_sha256": digest(executable), "checks": results,
              "limits": ["Host regular-file O_APPEND transport; no pipe atomicity guarantee.",
                         "Synthetic counters/native Vulkan fakes; no GPU, iPad or performance qualification.",
                         "A positive short write remains malformed and invalidates strict parsing; no tail repair."]}
    target = out / "report.json"
    target.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS exact-1 gate, full-width rows, 2000 concurrent JSON rows, 8000 competing writes, EINTR, short/error/overflow rejection; parser unchanged")
    print(target)


if __name__ == "__main__":
    main()
