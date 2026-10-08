#!/usr/bin/env python3
"""Isolated HLE62 actual old/new legacy AudioOut parity + bounded helper checks.

SDL queue/CVT and the guest pacing clock are original deterministic mocks.
Actual production prepareBuffer, queueAudio, registration and metadata helper
are compiled; this is neither a complete SDL conversion nor a device proof.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REL = Path("core/libs/prx/libSceAudioOut/src/AudioOut.cpp")

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def region(source):
    return source[source.index("enum class Format {"):source.index("static void sleepUs(")]

def extract_registration(source):
    first = source.index("            if (AudioOutIngressTrace::Enabled()) {")
    last = source.index('            static const bool trace = std::getenv("APS5_TRACE_AUDIOOUT")', first)
    return source[first:last]

def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}"

def original_bytes(first, frames, channels, bits):
    return bytes((frame * 17 + channel * 23 + byte * 7 + (frame // 1024) * 5) & 255
                 for frame in range(first, first + frames) for channel in range(channels) for byte in range(bits // 8))

def verify_side(side):
    shape = side["format"]
    assert side["captured_frames"] == 480000 and len(side["blocks"]) == 470
    assert sum(block["frames"] for block in side["blocks"]) == 480000
    for block in side["blocks"]:
        first, frames = block["frame_start"], block["frames"]
        data = original_bytes(first, frames, shape["channels"], shape["bits"])
        assert block["fnv64"] == fnv(data)
        assert block["phase"] == first % 1024 and block["window_frame_start"] == first - 257
        assert block["byte_start"] == first * shape["channels"] * (shape["bits"] // 8)
        assert block["sample_start"] == first * shape["channels"]
        width = shape["channels"] * (shape["bits"] // 8)
        for channel in range(shape["channels"]):
            one = b"".join(data[frame * width + channel * (shape["bits"] // 8):
                                frame * width + (channel + 1) * (shape["bits"] // 8)] for frame in range(frames))
            assert block["channel_fnv64"][channel] == fnv(one)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, default=ROOT / "upstreams/AnyPS5")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/audioout-ingress-trace-20261009/checks")
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = args.candidate.resolve() / REL
    if args.baseline:
        baseline = args.baseline.resolve() / REL
    else:
        baseline = out / "pinned-baseline" / REL
        baseline.parent.mkdir(parents=True)
        baseline.write_bytes(subprocess.check_output(
            ["git", "-C", str(ROOT / "upstreams/AnyPS5"), "show", "6e037e9899efb7439913a6e1151b901a92c9c9c8:" + str(REL)]))
    helper = source.with_name("AudioOutIngressTrace.hpp")
    fixture = ROOT / "tools/checks/audioout_ingress_trace.cpp"
    before, after = baseline.read_text(), source.read_text()
    assert sha(baseline) == "31bd2a3b896ba8334455aaaf994fbb9bfd3d525089e896274abcafcf75d62945"
    if args.candidate.resolve() != (ROOT / "upstreams/AnyPS5").resolve():
        assert sha(ROOT / "upstreams/AnyPS5" / REL) == sha(baseline), "Shared61 source must remain frozen during isolated qualification"
    constants = before[before.index("static constexpr int PORT_TYPE_MAIN"):before.index("enum class Format {")]
    registration = extract_registration(after)
    inc = out / "production_audioout_ingress.inc"
    production = "#define nanosleep fixture_nanosleep\n" + constants
    production += "\nnamespace Before {\n" + region(before) + "\n}\n"
    production += "\nnamespace After {\n" + region(after) + """
void RegisterProduction(Port& port, int i) {
    int type = port.type;
    std::uint32_t len = port.samplesNum, freq = port.freq;
    auto format = port.format;
""" + registration + "\n}\n}\n#undef nanosleep\n"
    inc.write_text(production)
    queue = after[after.index("static void queueAudio("):after.index("static void sleepUs(")]
    assert queue.index("AudioOutIngressTrace::Raw(") < queue.index("prepareBuffer(")
    assert queue.index("while (SDL_GetQueuedAudioSize(port.device) > minQueued)") < queue.index("AudioOutIngressTrace::Queued(") < queue.index("const int queueResult = SDL_QueueAudio(")
    assert "desired.callback = nullptr" in after and "AudioOut2" not in after
    command = shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++20", "-O1", "-g", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-function", "-Wno-unused-variable", "-pthread",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I" + str(source.parent), "-I" + str(ROOT / "upstreams/AnyPS5/3rdparty/SDL2/include"),
        "-I" + str(out), str(fixture), "-o", str(out / "audioout-ingress")]
    with (out / "compile.log").open("w") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    runs = []
    for option in (None, "0", "yes", "01", "true", "1"):
        label = option if option is not None else "unset"
        directory = out / ("run-" + label)
        directory.mkdir()
        env = dict(os.environ)
        env.pop("APS5_TRACE_AUDIOOUT_INGRESS", None)
        env.pop("APS5_AUDIOOUT_INGRESS_SKIP_FRAMES", None)
        if option is not None:
            env["APS5_TRACE_AUDIOOUT_INGRESS"] = option
        env["APS5_AUDIOOUT_INGRESS_DIRECTORY"] = str(directory / "actual")
        env["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        run = subprocess.run([str(out / "audioout-ingress"), str(directory)], env=env,
                             capture_output=True, text=True, timeout=90)
        log = out / ("run-" + label + ".log")
        log.write_text(run.stdout + run.stderr)
        assert run.returncode == 0, run.stderr
        result = json.loads(run.stdout)
        metas = [(p, json.loads(p.read_text())) for p in directory.rglob("port-*.json")]
        if option != "1":
            assert not metas and not (directory / "actual").exists()
        else:
            actual = [meta for p, meta in metas if "actual" in p.parts]
            complete = next(meta for meta in actual if meta["result"] == "complete")
            assert complete["raw"]["captured_frames"] == complete["queued"]["captured_frames"] == 480000
            assert len(complete["calls"]) == 1875 and all(call["sdl_result_known"] and call["sdl_result"] == 0 for call in complete["calls"])
            # Independent exact integer-to-float bytes for the actual queue path.
            for side in ("raw", "queued"):
                for block in complete[side]["blocks"]:
                    first, frames = block["frame_start"], block["frames"]
                    data = b"".join(struct.pack("<f", ((frame * 17 + channel * 29) % 4096) / 4096.0 + channel * .125)
                                    for frame in range(first, first + frames) for channel in range(2))
                    assert block["fnv64"] == fnv(data)
            # A deliberately changed guest source during the pacing wait proves
            # that raw-before-prepare and queue-after-wait are distinct captures.
            paced = next(meta for meta in actual if meta["port"] == 4)
            call = paced["calls"][0]
            initial = b"".join(struct.pack("<f", (index + 1) / 128) for index in range(16))
            assert call["raw"]["fnv64"] == fnv(initial)
            assert call["queued"]["fnv64"] == fnv(bytes([0x3c]) * 64)
            assert call["queue_aliases_raw"] and call["sdl_result_known"]
            independent = next(meta for _, meta in metas if meta["port"] == 101)
            verify_side(independent["raw"]); verify_side(independent["queued"])
            assert all(meta["raw_pcm_retained"] is False and meta["hash_equality_proves_byte_identity"] is False for _, meta in metas)
            reasons = {meta["result"] for _, meta in metas}
            assert {"null-drain", "null-drain-clear", "pacing-timeout-clear", "cvt-build-failure", "cvt-convert-failure",
                    "sdl-queue-failure", "queue-partial-frame", "shape-or-byte-limit", "call-limit",
                    "cancel-between-raw-and-queue"} <= reasons
            assert not list(directory.rglob("*.partial")) and not list(directory.rglob("*.wav"))
            result["metadataHashMap"] = {str(p.relative_to(directory)): sha(p) for p, _ in metas}
        result.update(option=option, logSha256=sha(log))
        runs.append(result)
    io_checks = []
    for mode in ("--io-exclusive", "--io-partial"):
        directory = out / mode[2:]
        env = dict(os.environ, ASAN_OPTIONS="halt_on_error=1:abort_on_error=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        run = subprocess.run([str(out / "audioout-ingress"), str(directory), mode], env=env, capture_output=True, text=True, timeout=30)
        log = out / (mode[2:] + ".log")
        log.write_text(run.stdout + run.stderr)
        assert run.returncode == 0, run.stderr
        item = json.loads(run.stdout); item["mode"] = mode; item["logSha256"] = sha(log); io_checks.append(item)
    # Matched correcting negative: remove only actual raw ingress hook.
    negative = out / "negative-missing-raw"
    negative.mkdir()
    broken = production.replace("auto ingress = AudioOutIngressTrace::Raw(port.ingressTrace, data, port.samplesNum);",
                                "AudioOutIngressTrace::Token ingress;")
    assert broken != production
    (negative / inc.name).write_text(broken)
    negative_command = [str(negative / "audioout-ingress") if arg == str(out / "audioout-ingress")
                        else "-I" + str(negative) if arg == "-I" + str(out) else arg for arg in command]
    with (negative / "compile.log").open("w") as log:
        subprocess.run(negative_command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    negative_env = dict(os.environ, APS5_TRACE_AUDIOOUT_INGRESS="1", APS5_AUDIOOUT_INGRESS_DIRECTORY=str(negative / "actual"))
    negative_env.pop("APS5_AUDIOOUT_INGRESS_SKIP_FRAMES", None)
    run = subprocess.run([str(negative / "audioout-ingress"), str(negative)], env=negative_env, capture_output=True, text=True, timeout=90)
    (negative / "run.log").write_text(run.stdout + run.stderr)
    assert run.returncode != 0 and "production raw hook complete" in run.stderr
    report = {
        "status": "passed", "scope": "actual legacy prepareBuffer/queueAudio/registration and bounded helper; original SDL/CVT/pacing mocks",
        "sha256": {str(p.relative_to(ROOT)): sha(p) for p in [source, baseline, helper, fixture, Path(__file__).resolve(), inc, out / "audioout-ingress", out / "compile.log"]},
        "compileCommand": command, "runs": runs, "exclusivePrivateExportChecks": io_checks,
        "negativeControl": {"removed": "actual raw ingress hook only", "exitCode": run.returncode, "logSha256": sha(negative / "run.log")},
        "deviceTested": False, "audioFix": False, "rawPcmExported": False,
        "limits": [
            "Mock SDL CVT/queue behavior does not qualify the actual SDL resampler, full DLL process teardown or iPad.",
            "FNV fingerprints do not prove byte identity or audio defect origin.",
            "Raw requested-frame and queued whole-frame ordinals/clocks are independent.",
            "480000 frames are ten rendered seconds only at48kHz; deadline uses skip/48000+45 wall seconds.",
            "Diagnostic hashing/mutex/worker overhead and hardware audio quality remain unmeasured.",
            "Shared61 source is unchanged; candidate is isolated and not promoted."
        ]
    }
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": "passed", "checks": [run["checks"] for run in runs], "report": str(out / "report.json")}))

if __name__ == "__main__":
    main()
