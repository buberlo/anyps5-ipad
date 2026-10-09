#!/usr/bin/env python3
"""Check actual legacy AudioOut frame pacing with deterministic host mocks.

The prepatch baseline is reconstructed by reversing the canonical patch in a
fresh ignored output directory. No Git, DLL/full build or device is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REL = Path("core/libs/prx/libSceAudioOut/src")
PATCH = ROOT / "patches/anyps5/0065-audioout-frame-pacing.patch"
PREFIX = ROOT / "tools/checks/audioout_frame_pacing_prefix.cpp"
FIXTURE = ROOT / "tools/checks/audioout_frame_pacing.cpp"
SOURCE_SHA256 = {
    "baseline": "2b5e22662f2740663e068055bbbd89a3ca7ea0d641c33b02a7b200ed4e22982c",
    "candidate": "efa4883563c08b48bfa32326a34da2b5c72acf399915d192a629bf007c914539",
}
HELPER_SHA256 = "8b74e391673af282fabf5bc44d4a14634deb77cb143d47a0e2c59275def5d6e7"
PATCH_SHA256 = "d119e02b7edec8ffeba6edc035fea3fca504e7e39a84b4c7fc360f227947bd95"
# Immutable actual-function identities. Intentional production changes require
# reviewing these fixtures and updating their extraction provenance together.
FUNCTION_HASHES = {
    "baseline": {
        "static bool formatIsFloat(": "1593b327223c8f7e1df97b51a07f6388cb8df0a879921e17386bc492a9bb16ea",
        "static bool formatIsStd(": "37a9fd6976f7c840bd14adbadf3dce36083ebd06c7d472ae07bcda8cda0a35e5",
        "static int channelsForFormat(": "349807496817e64919450f4dc5593a0afdb9c10ceeed7c971b6a4ed00167480f",
        "static SDL_AudioFormat sdlFormat(": "25730ae2e501544eb183601cb6d8a8eb5e0ada59b27452ee42de1f6a0ecf6b98",
        "static std::uint32_t bytesPerSample(": "a7885f70030d4c2c5bfdca5dc08b9cf32daaf1fff20711f35873ff97d6a378b9",
        "static bool ensureSdlAudio(": "91c2e75c4baae1f57fc78d7e7d964912227cebcc0c26a438587bd8626ba5f328",
        "static bool openDevice(": "f3169df00d4934ec448d8dd020551492e2eb42d176ea664dd24a57a0dbaa53d4",
        "static void closeDevice(": "601ce6aba339890d85e25553e74bc1a0d7fa184291aa50e0d599326d0c8618fc",
        "static const void* prepareBuffer(": "57e7b1e7d73ddfc200230ba4d1a6d69fbab7b2e2ec1a5fa689a4b7b61f4b5692",
        "static void queueAudio(": "cc7b5b5bae163a94fc961fe9e49559d2f773997c93097da725f5b6cb185edd81",
        "static void sleepUs(std::uint64_t us) {": "c28a8f7d0b047b3724c505002303e984ef5767331a458ee2e9132247650b5dc8",
        "static void paceVirtualPort(": "12faab851219e28011f63e0c378a4891948a90c3dfa5221def9f826249b2bdb8",
        "static bool portTypeValid(": "20a0224f4734eac4a4b16b07e248a0aa2a25087581747ebcbcbbb111c221a485",
        "static Port* getPort(": "91eb14ab9f0c3807c3a14945b79ecd9a72669b470ef78906df00fd64a2c26aa7",
        "int APS5_VABI sceAudioOutOpen(": "4781775f8b7ec7d0be91b0b701a07baee5f6a5985ac9e1c674ae0487e5303613",
        "int APS5_VABI sceAudioOutClose(": "968e843259b98a46773e52f7c8a64ce408aae7e0070b7fd298a1cc0ecf007f98",
        "int APS5_VABI sceAudioOutOutput(": "0097eba150b20f82d72264da8cde27ea69d040b1b63ca517f4784103762ee16a",
        "int APS5_VABI sceAudioOutOutputs(": "b0a55f0d716f71a0e5caf0006fddcd463f65172cc59cf8ab22b75c34eaf676e0"
    },
    "candidate": {
        "static bool formatIsFloat(": "1593b327223c8f7e1df97b51a07f6388cb8df0a879921e17386bc492a9bb16ea",
        "static bool formatIsStd(": "37a9fd6976f7c840bd14adbadf3dce36083ebd06c7d472ae07bcda8cda0a35e5",
        "static int channelsForFormat(": "349807496817e64919450f4dc5593a0afdb9c10ceeed7c971b6a4ed00167480f",
        "static SDL_AudioFormat sdlFormat(": "25730ae2e501544eb183601cb6d8a8eb5e0ada59b27452ee42de1f6a0ecf6b98",
        "static std::uint32_t bytesPerSample(": "a7885f70030d4c2c5bfdca5dc08b9cf32daaf1fff20711f35873ff97d6a378b9",
        "static bool ensureSdlAudio(": "91c2e75c4baae1f57fc78d7e7d964912227cebcc0c26a438587bd8626ba5f328",
        "static bool openDevice(": "f3169df00d4934ec448d8dd020551492e2eb42d176ea664dd24a57a0dbaa53d4",
        "static void closeDevice(": "601ce6aba339890d85e25553e74bc1a0d7fa184291aa50e0d599326d0c8618fc",
        "static const void* prepareBuffer(": "57e7b1e7d73ddfc200230ba4d1a6d69fbab7b2e2ec1a5fa689a4b7b61f4b5692",
        "static void queueAudio(": "170fb1841dcdb9aa401ad5bc31ea5705a97d57cad6b8b92964036ee654183251",
        "static void sleepUs(std::uint64_t us) {": "c28a8f7d0b047b3724c505002303e984ef5767331a458ee2e9132247650b5dc8",
        "static void paceVirtualPort(": "12faab851219e28011f63e0c378a4891948a90c3dfa5221def9f826249b2bdb8",
        "static bool portTypeValid(": "20a0224f4734eac4a4b16b07e248a0aa2a25087581747ebcbcbbb111c221a485",
        "static Port* getPort(": "91eb14ab9f0c3807c3a14945b79ecd9a72669b470ef78906df00fd64a2c26aa7",
        "int APS5_VABI sceAudioOutOpen(": "5c59de4955c20b940fe8db30e6ef78858e203b96798c4faebc33694e68298a05",
        "int APS5_VABI sceAudioOutClose(": "968e843259b98a46773e52f7c8a64ce408aae7e0070b7fd298a1cc0ecf007f98",
        "int APS5_VABI sceAudioOutOutput(": "0097eba150b20f82d72264da8cde27ea69d040b1b63ca517f4784103762ee16a",
        "int APS5_VABI sceAudioOutOutputs(": "b0a55f0d716f71a0e5caf0006fddcd463f65172cc59cf8ab22b75c34eaf676e0"
    }
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(source, needle):
    require(source.count(needle) == 1, "unrecognized/ambiguous extraction: " + needle)
    start = source.index(needle)
    opening = source.index("{", start)
    depth, position = 1, opening + 1
    while depth:
        require(position < len(source), "unterminated extraction: " + needle)
        depth += (source[position] == "{") - (source[position] == "}")
        position += 1
    return source[start:position]


def generate(source, kind, output):
    text = source.read_text()
    chunks = [extract(text, needle) for needle in FUNCTION_HASHES[kind]]
    hashes = {needle: hashlib.sha256(chunk.encode()).hexdigest()
              for needle, chunk in zip(FUNCTION_HASHES[kind], chunks)}
    require(hashes == FUNCTION_HASHES[kind], kind + " actual function identity changed")
    require(sha(source) == SOURCE_SHA256[kind], kind + " source identity changed")
    constants = text[text.index("static constexpr int PORT_TYPE_MAIN"):text.index("enum class Format")]
    enum = extract(text, "enum class Format") + ";"
    port = extract(text, "struct Port") + ";"
    mapping = re.search(r"static constexpr std::uint32_t STD_8CH_MAP\[8\] = .*?;", text)
    require(mapping is not None, "missing actual channel mapping")
    generated = output / (kind + ".cpp")
    generated.write_text(PREFIX.read_text() + "\n" + constants + enum + "\n" + port +
                         "\nstatic std::mutex g_mutex;static Port g_ports[PORTS_MAX];static bool g_sdlInitialized=false;\n" +
                         mapping[0] + "\nstatic void sleepUs(std::uint64_t us);\n" +
                         "\n\n".join(chunks) + "\n" + FIXTURE.read_text())
    return generated, {"sourceSha256": sha(source), "generatedSha256": sha(generated),
                       "verbatimFunctions": hashes}


def checked(command, log, cwd=None, timeout=120):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    log.write_text(result.stdout + result.stderr)
    require(result.returncode == 0, "command failed; see " + str(log))
    return result


def compile_fixture(compiler, source, binary, include, candidate, sanitizer, log):
    command = compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                          "-Wno-unused-variable", "-UNDEBUG", "-O2", "-pthread"]
    if sanitizer:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    command += ["-DCANDIDATE=" + str(int(candidate)), "-I" + str(include), str(source), "-o", str(binary)]
    checked(command, log)
    return command


def fixture_run(binary, gate, log):
    environment = dict(os.environ, APS5_AUDIOOUT_FRAME_PACING=gate,
                       ASAN_OPTIONS="detect_leaks=0:halt_on_error=1:abort_on_error=1",
                       UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    # Unrelated diagnostics must not alter the fixture's serialized parity.
    environment.pop("APS5_TRACE_AUDIOOUT", None)
    result = subprocess.run([str(binary), gate], env=environment, capture_output=True, text=True, timeout=30)
    log.write_text(result.stdout + result.stderr)
    return result


def summary(result):
    match = re.search(r"PASS assertions=(\d+)", result.stdout)
    require(match is not None, "fixture success receipt missing")
    fixtures = []
    for line in result.stdout.splitlines():
        if line.startswith(("LONGRUN ", "RATIONAL ")):
            label, *values = line.split()
            fixtures.append({"fixture": label, **{key: int(value) for key, value in
                             (pair.split("=") for pair in values)}})
    return {"assertions": int(match[1]), "fixtures": fixtures}


def main():
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("--source", type=Path, default=ROOT / "upstreams/AnyPS5")
    arguments.add_argument("--output", type=Path, required=True, help="Fresh directory beneath ignored build/")
    args = arguments.parse_args()
    output = args.output.resolve()
    source = args.source.resolve()
    require(ROOT / "build" in output.parents and not output.exists(), "fresh ignored build/ output required")
    require(source not in output.parents, "output must be separate from production source")
    actual = source / REL / "AudioOut.cpp"
    helper = source / REL / "AudioOutFramePacing.hpp"
    require(sha(actual) == SOURCE_SHA256["candidate"] and sha(helper) == HELPER_SHA256,
            "expected production frame-pacing source/helper required; apply canonical patch first")
    require(sha(PATCH) == PATCH_SHA256, "canonical patch identity changed")
    before = {str(path): sha(path) for path in (actual, helper, PATCH, PREFIX, FIXTURE)}
    text = actual.read_text()
    queue = extract(text, "static void queueAudio(")
    require(queue.index("port.framePacing.Remaining(") < queue.index("AudioOutIngressTrace::Raw(") <
            queue.index("prepareBuffer("), "actual cadence wait must precede raw hook/preparation")
    require("if (framePaced) port.framePacing.Commit(sceKernelGetProcessTime());" in queue,
            "actual successful queue cadence commit missing")
    patch_paths = {line[4:].split("\t", 1)[0][2:] for line in PATCH.read_text().splitlines()
                   if line.startswith(("--- a/", "+++ b/"))}
    require(patch_paths == {str(REL / "AudioOut.cpp"), str(REL / "AudioOutFramePacing.hpp")},
            "canonical patch touches unexpected paths")
    output.mkdir(parents=True, exist_ok=False)
    for kind in ("baseline", "candidate"):
        destination = output / kind / REL
        destination.mkdir(parents=True)
        shutil.copyfile(actual, destination / actual.name)
        shutil.copyfile(helper, destination / helper.name)
    command = ["patch", "--batch", "--fuzz=0", "-R", "-p1", "-i", str(PATCH)]
    checked(command, output / "reverse-patch.log", output / "baseline")
    require(not (output / "baseline" / REL / helper.name).exists(), "reverse patch did not remove new helper")
    require(sha(output / "baseline" / REL / actual.name) == SOURCE_SHA256["baseline"], "reverse patch baseline differs")
    compiler = shlex.split(os.environ.get("CXX", "clang++"))
    require(compiler, "CXX compiler command required")
    runs, receipts, commands = [], {}, []
    for kind in ("baseline", "candidate"):
        generated, receipts[kind] = generate(output / kind / REL / actual.name, kind, output)
        for variant in ("strict", "asan-ubsan"):
            binary = output / (kind + "-" + variant)
            commands.append(compile_fixture(compiler, generated, binary, output / "candidate" / REL,
                                           kind == "candidate", variant == "asan-ubsan",
                                           output / (binary.name + "-compile.log")))
            for gate in (("0", "1") if kind == "candidate" else ("0",)):
                log = output / (binary.name + "-gate" + gate + ".log")
                result = fixture_run(binary, gate, log)
                require(result.returncode == 0, "fixture failed; see " + str(log))
                runs.append({"kind": kind, "variant": variant, "gate": gate, "exitCode": result.returncode,
                             "logSha256": sha(log), "binarySha256": sha(binary), **summary(result)})
    for variant in ("strict", "asan-ubsan"):
        baseline = (output / ("baseline-" + variant + "-gate0.log")).read_text().splitlines()[0]
        candidate = (output / ("candidate-" + variant + "-gate0.log")).read_text().splitlines()[0]
        require(baseline == candidate and baseline.startswith("PARITY "), "default-off serialized parity differs")
    # Matched semantic negatives touch private copied inputs only. Compilation
    # success is required; only the expected runtime assertion counts as a hit.
    mutations = [
        ("helper-fraction", "fraction = numerator % rate;", "fraction = 0;", "third rational step", True),
        ("missing-cadence-commit", "if (framePaced) port.framePacing.Commit(sceKernelGetProcessTime());",
         "if (framePaced) {}", "steady no burst under existing1ms guard", False),
        ("missing-raw-hook", "auto ingress = AudioOutIngressTrace::Raw(port.ingressTrace, data, port.samplesNum);",
         "AudioOutIngressTrace::Token ingress;", "two thousand actual Output calls", False),
    ]
    negatives = []
    for label, old, new, expected_failure, mutate_helper in mutations:
        directory = output / label
        directory.mkdir()
        cpp = directory / "candidate.cpp"
        hpp = directory / helper.name
        cpp.write_bytes((output / "candidate.cpp").read_bytes())
        hpp.write_bytes(helper.read_bytes())
        mutated = hpp if mutate_helper else cpp
        text = mutated.read_text()
        require(text.count(old) == 1, "semantic negative target changed: " + label)
        mutated.write_text(text.replace(old, new))
        binary = directory / "fixture"
        commands.append(compile_fixture(compiler, cpp, binary, directory, True, False, directory / "compile.log"))
        result = fixture_run(binary, "1", directory / "run.log")
        require(result.returncode == 1 and "FAIL " + expected_failure in result.stderr,
                "semantic negative did not fail as expected: " + label)
        negatives.append({"mutation": label, "expectedFailure": expected_failure, "exitCode": result.returncode,
                          "logSha256": sha(directory / "run.log"), "mutatedInputSha256": sha(mutated)})
    require(before == {str(path): sha(path) for path in (actual, helper, PATCH, PREFIX, FIXTURE)},
            "production/public inputs changed during check")
    report = {"status": "actual_source_frame_pacing_host_checks_passed", "functionsExtractedPerVariant": 18,
              "sourceExtraction": receipts, "helperSha256": sha(helper), "patchSha256": sha(PATCH),
              "fixtureSha256": {"prefix": sha(PREFIX), "fixture": sha(FIXTURE), "checker": sha(Path(__file__))},
              "compileCommands": commands, "runs": runs, "semanticNegatives": negatives,
              "defaultOffExactSerializedParity": True, "productionInputsUnchanged": True,
              "deviceExecuted": False, "audioQualityAccepted": False, "performanceAccepted": False,
              "limits": ["Deterministic SDL/CVT and clock mocks do not qualify real resampling or device timing.",
                         "No PS5 scheduling contract, full DLL build, GPU, gameplay, audio-quality or causal-repair proof.",
                         "ASan leak detection is disabled for platforms where it is unsupported."]}
    path = output / "report.json"
    path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "report": str(path), "reportSha256": sha(path),
                      "runs": len(runs), "semanticNegatives": len(negatives)}))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
