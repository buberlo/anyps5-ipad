#!/usr/bin/env python3
"""Check bounded postmix capture with original synthetic audio, never a device.

Actual old/new mixer/render callback text is compiled using explicit Core Audio
declaration mocks. Worker, WAV/export, atomics and capture code are production.
This does not qualify an AudioUnit's ABI, hardware output or audible quality.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def struct_source(text, name):
    start = text.index("struct " + name + " {")
    return text[start:text.index("\n};", start) + 3]


def callback_source(text):
    start = text.index("static void ios_mix_stream(")
    return text[start:text.index("/* Parse the WASAPI format", start)]


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path,
                        default=root / "build/audio-postmix-capture-20261009/checks")
    parser.add_argument("--baseline", type=Path,
                        default=root / "build/audio-postmix-capture-20261009/baseline/build/ntdll-unix/audio_null_ios.c")
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = root / "upstreams/Madeira/build/ntdll-unix/audio_null_ios.c"
    header = source.parent / "aps5_audio_postmix_capture.h"
    fixture = root / "tools/checks/audio_postmix_capture.c"
    before, after = args.baseline.read_text(), source.read_text()
    assert digest(args.baseline) == "87d58baf633fe7f1ca6b6b14c549d46666b9548cdc704cd4fcd40d7b08f93640"
    for name in ("ios_stream", "ios_audio_device"):
        assert struct_source(before, name) == struct_source(after, name)
    old, current = callback_source(before), callback_source(after)
    assert old.split("static OSStatus ios_audio_render_cb")[0] == current.split("static OSStatus ios_audio_render_cb")[0]
    old = old.replace("ios_mix_stream(", "ios_mix_stream_before(")
    old = old.replace("ios_audio_render_cb(", "ios_audio_render_cb_before(")
    inc = out / "production_audio_callback.inc"
    inc.write_text("\n".join([
        struct_source(after, "ios_stream"), struct_source(after, "ios_audio_device"),
        "static struct ios_audio_device g_dev;",
        "static _Atomic(struct ios_stream *) g_mix[IOS_MAX_STREAMS];",
        "static struct aps5_postmix_capture g_audio_postmix_capture;", old, current]))
    # Structural check of the actual RT helper, including its publication helper.
    rt = header.read_text().split("static void aps5_postmix_publish_done", 1)[1].split("/* Worker-only cancellation", 1)[0]
    prohibited = r"\b(calloc|malloc|free|pthread_\w+|nanosleep|clock_gettime|fprintf|openat|write|close)\s*\("
    assert not re.search(prohibited, rt)
    assert re.search(prohibited, "pthread_mutex_lock(&lock);")
    compiler = shlex.split(os.environ.get("CC", "cc"))
    executable = out / "audio-postmix-capture"
    command = compiler + ["-std=c11", "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE", "-O1", "-g",
                          "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-pthread",
                          "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                          "-I" + str(header.parent), "-I" + str(out), str(fixture), "-o", str(executable)]
    with (out / "compile.log").open("w") as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    results = []
    for flag in (None, "0", "yes", "01", "true", "1"):
        label = flag if flag is not None else "unset"
        directory = out / ("run-" + label)
        directory.mkdir()
        env = dict(os.environ)
        env.pop("MADEIRA_AUDIO_CAPTURE_POSTMIX", None)
        if flag is not None:
            env["MADEIRA_AUDIO_CAPTURE_POSTMIX"] = flag
        env["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        run = subprocess.run([str(executable), str(directory)], env=env,
                             capture_output=True, text=True, check=True, timeout=30)
        (out / ("run-" + label + ".log")).write_text(run.stdout + run.stderr)
        result = json.loads(run.stdout)
        result["flag"] = flag
        result["log_sha256"] = digest(out / ("run-" + label + ".log"))
        worker_files = list((directory / "anyps5-audio-captures").glob("*.wav")) if flag == "1" else []
        assert len(worker_files) == (1 if flag == "1" else 0)
        if flag == "1":
            wav = worker_files[0]
            data = wav.read_bytes()
            meta = json.loads(wav.with_suffix(".json").read_text())
            assert len(data) == 58 + 480000 * 8 and data[:4] == b"RIFF"
            assert struct.unpack_from("<I", data, 4)[0] == len(data) - 8
            assert struct.unpack_from("<HHIIHHH", data, 20) == (3, 2, 48000, 384000, 8, 32, 0)
            assert meta["result"] == "complete" and meta["captured_frames"] == 480000
            assert meta["record_count"] == 469 and len(meta["callbacks"]) == 469
            assert meta["callbacks"][-1]["captured_frames"] == 768
            assert all(x["host_time_valid"] and x["captured_offset"] == i * 1024
                       for i, x in enumerate(meta["callbacks"]))
            assert b"microphone" not in run.stderr.encode()
            result["worker_wav_sha256"] = digest(wav)
            result["worker_metadata_sha256"] = digest(wav.with_suffix(".json"))
        elif run.stderr:
            raise AssertionError(run.stderr)
        for p in directory.glob("export-*.json"):
            meta = json.loads(p.read_text())
            assert meta["callbacks"][0]["sample_time"] is None
            assert meta["requested_frames"] == meta["captured_frames"] == 8
        results.append(result)
    # A matched control substitutes only the original callback and must fail
    # the captured-output assertion. Its mixer/output path is still identical.
    negative = out / "baseline-control"
    negative.mkdir()
    negative_inc = negative / "production_audio_callback.inc"
    negative_inc.write_text("\n".join([
        struct_source(after, "ios_stream"), struct_source(after, "ios_audio_device"),
        "static struct ios_audio_device g_dev;",
        "static _Atomic(struct ios_stream *) g_mix[IOS_MAX_STREAMS];",
        "static struct aps5_postmix_capture g_audio_postmix_capture;",
        old, callback_source(before)]))
    negative_executable = negative / "audio-postmix-capture"
    negative_command = ["-I" + str(negative) if x == "-I" + str(out)
                        else str(negative_executable) if x == str(executable) else x for x in command]
    with (negative / "compile.log").open("w") as log:
        subprocess.run(negative_command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    env.pop("MADEIRA_AUDIO_CAPTURE_POSTMIX", None)
    rejected = subprocess.run([str(negative_executable), str(negative)], env=env,
                              capture_output=True, text=True, timeout=30)
    (negative / "run.log").write_text(rejected.stdout + rejected.stderr)
    assert rejected.returncode != 0 and "g_audio_postmix_capture.frames == expected" in rejected.stderr
    report = {"schema": 1, "status": "passed", "scope": "synthetic native capture, extracted actual callbacks and Core Audio declaration mocks",
              "baseline_sha256": digest(args.baseline), "source_sha256": digest(source),
              "header_sha256": digest(header), "fixture_sha256": digest(fixture),
              "script_sha256": digest(Path(__file__)), "extracted_callback_sha256": digest(inc),
              "executable_sha256": digest(executable), "compile_command": command,
              "compile_log_sha256": digest(out / "compile.log"), "runs": results,
              "baseline_callback_negative_control": {
                  "exit_code": rejected.returncode, "captured_output_assertion_rejected": True,
                  "extracted_callback_sha256": digest(negative_inc),
                  "executable_sha256": digest(negative_executable), "compile_command": negative_command,
                  "compile_log_sha256": digest(negative / "compile.log"),
                  "log_sha256": digest(negative / "run.log")},
              "rt_no_allocation_io_clock_lock_calls_checked": True,
              "permanently_stalled_producer_boundary": "Worker cancellation never reads or frees active producer arrays; permanently owned participant admission can delay export indefinitely while RAM remains bounded.",
              "device_tested": False, "audible_quality_accepted": False, "github_actions_used": False}
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": "passed", "runs": len(results),
                      "checks_per_run": [x["checks"] for x in results],
                      "report": str(out / "report.json")}))


if __name__ == "__main__":
    main()
