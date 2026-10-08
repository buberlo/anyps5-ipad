#!/usr/bin/env python3
"""Focused native50 checks against the isolated candidate, never a device.

Actual before/after native ReleaseBuffer and render callbacks are extracted;
Core Audio declarations in the original fixture are mocks. The trace helper,
hashing, mutexes, worker and private JSON export are production code.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def structure(text, name):
    start = text.index("struct " + name + " {")
    return text[start:text.index("\n};", start) + 3]


def release(text):
    start = text.index("static NTSTATUS ios_release_render_buffer(")
    return text[start:text.index("static NTSTATUS ios_get_capture_buffer(", start)]


def callback(text):
    start = text.index("static void ios_mix_stream(")
    return text[start:text.index("/* Parse the WASAPI format", start)]


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, default=root / "build/audio-release-trace-20261009/candidate")
    parser.add_argument("--baseline", type=Path, default=root / "build/audio-release-trace-20261009/baseline")
    parser.add_argument("--output-dir", type=Path, default=root / "build/audio-release-trace-20261009/checks")
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = args.candidate.resolve() / "build/ntdll-unix/audio_null_ios.c"
    baseline = args.baseline.resolve() / "build/ntdll-unix/audio_null_ios.c"
    header = source.parent / "aps5_audio_release_trace.h"
    fixture = root / "tools/checks/audio_release_trace.c"
    before, after = baseline.read_text(), source.read_text()
    assert digest(baseline) == "f34e9e8891b7ffce8aebc8982e3eaadd39d8beaf755865a1fa2e1f99a80444d4"
    assert callback(before) == callback(after), "Production RT code must be byte-identical"
    for name in ("ios_stream", "ios_audio_device"):
        assert structure(before, name) == structure(after, name)
    start = after.index("static pthread_once_t g_audio_release_once")
    wrappers = after[start:after.index("static _Atomic int64_t aps5_device_period", start)]
    old_release = release(before).replace("ios_release_render_buffer(", "ios_release_render_buffer_before(")
    old_callback = callback(before).replace("ios_mix_stream(", "ios_mix_stream_before(").replace("ios_audio_render_cb(", "ios_audio_render_cb_before(")
    inc = out / "production_audio_release.inc"
    inc.write_text("\n".join([
        structure(after, "ios_stream"), structure(after, "ios_audio_device"),
        "static struct ios_audio_device g_dev;",
        "static _Atomic(struct ios_stream *) g_mix[IOS_MAX_STREAMS];",
        "static struct aps5_postmix_capture g_audio_postmix_capture;", wrappers,
        "static struct ios_stream *stream_from_handle(uint64_t h) { return (struct ios_stream *)(uintptr_t)h; }",
        "static int ios_stream_is_live(const struct ios_stream *s) { return s && s->valid && s->started; }",
        old_release, release(after), old_callback, callback(after)]))
    hook = release(after)
    assert hook.index("memset(s->render_scratch") < hook.index("ios_release_trace_record(") < hook.index("while (i < n)")
    body = header.read_text().split("static void aps5_release_record(", 1)[1].split("/* Worker-only export", 1)[0]
    assert not re.search(r"\b(calloc|malloc|free|fprintf|openat|write|fwrite|fdopen)\s*\(", body)
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-function", "-pthread", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I" + str(source.parent), "-I" + str(out), str(fixture), "-o", str(out / "audio-release-trace")]
    with (out / "compile.log").open("w") as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    results = []
    for flag in (None, "0", "yes", "01", "true", "1"):
        label = flag if flag is not None else "unset"
        directory = out / ("run-" + label)
        directory.mkdir()
        env = dict(os.environ)
        env.pop("MADEIRA_AUDIO_TRACE_RELEASE", None)
        if flag is not None:
            env["MADEIRA_AUDIO_TRACE_RELEASE"] = flag
        env["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        run = subprocess.run([str(out / "audio-release-trace"), str(directory)], env=env,
                             capture_output=True, text=True, timeout=90)
        run_log = out / ("run-" + label + ".log")
        run_log.write_text(run.stdout + run.stderr)
        assert run.returncode == 0, run.stderr
        result = json.loads(run.stdout)
        result.update(flag=flag, log_sha256=digest(run_log))
        exported = list((directory / "anyps5-audio-captures").glob("release-*.json"))
        assert len(exported) == (2 if flag == "1" else 0)
        if flag == "1":
            metas = [json.loads(p.read_text()) for p in exported]
            completed = next(m for m in metas if m["result"] == "complete")
            assert completed["traced_frames"] == 480000 and completed["block_count"] == 469
            assert completed["blocks"][-1]["frames"] == 768
            assert sum(x["frames"] for x in completed["blocks"]) == 480000
            assert all(x["source_block_phase"] == 0 for x in completed["blocks"])
            assert len(set(x["fnv64"] for x in completed["blocks"][:-1])) > 1
            assert completed["hash_equality_proves_byte_identity"] is False
            assert completed["raw_pcm_retained"] is False
            for block in completed["blocks"]:
                pos = block["source_frame_start"]
                assert block["source_byte_start"] == pos * 8
                assert block["source_sample_start"] == pos * 2
                assert block["window_frame_start"] == pos
                expected = 14695981039346656037
                for frame in range(pos, pos + block["frames"]):
                    for byte in range(8):
                        value = (frame * 17 + byte * 23 + (frame // 1024) * 5) & 255
                        expected = ((expected ^ value) * 1099511628211) & ((1 << 64) - 1)
                assert block["fnv64"] == f"{expected:016x}"
            assert sum(x["traced_frames"] for x in completed["submissions"]) == 480000
            assert all(x["byte_count"] == x["traced_frames"] * 8 and
                       x["byte_start"] == (x["source_frame_start"] + x["skipped_frames"]) * 8
                       for x in completed["submissions"])
            assert not list(directory.rglob("*.wav"))
            assert next(m for m in metas if m["result"] == "teardown")["zero_submissions"] > 0
            result["export_hashes"] = {p.name: digest(p) for p in exported}
        else:
            assert not (directory / "anyps5-audio-captures").exists()
        results.append(result)
    # Matched source negative control removes only the ReleaseBuffer hook.
    # The same fixture must fail its source-trace assertion despite parity.
    negative = out / "baseline-control"
    negative.mkdir()
    negative_inc = negative / inc.name
    text = inc.read_text()
    current = release(after)
    text = text.replace(current, release(before))
    negative_inc.write_text(text)
    negative_exe = negative / "audio-release-trace"
    negative_command = ["-I" + str(negative) if x == "-I" + str(out)
                        else str(negative_exe) if x == str(out / "audio-release-trace") else x for x in command]
    with (negative / "compile.log").open("w") as log:
        subprocess.run(negative_command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    negative_env = dict(os.environ, MADEIRA_AUDIO_TRACE_RELEASE="1")
    rejected = subprocess.run([str(negative_exe), str(negative)], env=negative_env,
                              capture_output=True, text=True, timeout=90)
    (negative / "run.log").write_text(rejected.stdout + rejected.stderr)
    assert rejected.returncode != 0 and "zero_submissions" in rejected.stderr
    report = {"schema": 1, "status": "passed", "scope": "isolated production native release/helper/worker with original synthetic bytes and Core Audio declaration mocks",
              "baseline_sha256": digest(baseline), "source_sha256": digest(source), "header_sha256": digest(header),
              "fixture_sha256": digest(fixture), "script_sha256": digest(Path(__file__)), "extracted_sha256": digest(inc),
              "compile_command": command, "compile_log_sha256": digest(out / "compile.log"),
              "executable_sha256": digest(out / "audio-release-trace"), "runs": results,
              "rt_source_unchanged": True, "source_hook_allocation_io_absent": True,
              "negative_control": {"exit_code": rejected.returncode, "assertion_rejected": True,
                                   "log_sha256": digest(negative / "run.log")},
              "device_tested": False, "sound_quality_accepted": False,
              "limits": ["Synthetic native Core Audio declarations do not prove hardware output or audible quality.",
                         "Fingerprints alone cannot prove byte identity; source and postmix clocks have separate phases.",
                         "Mutex and hashing are non-RT; diagnostic overhead is unmeasured.",
                         "One fixed pool remains process-owned after the bounded worker exits; metadata generations are never recycled."]}
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": "passed", "checks": [x["checks"] for x in results], "report": str(out / "report.json")}))


if __name__ == "__main__":
    main()
