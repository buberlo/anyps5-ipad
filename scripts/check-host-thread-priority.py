#!/usr/bin/env python3
"""Check production HLE priority mapping/startup/setter using WinAPI mocks.

Actual Thread.cpp bodies are extracted into a fresh build directory. No WinAPI,
guest, Wine process or device is executed by these original host fixtures.
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


def extract(source, name, structure=False):
    pattern = (r"^struct\s+" + name + r"\s*\{" if structure else
               r"^[^\n;{}]*\b" + name + r"\s*\([^;{}]*\)\s*\{")
    match = re.search(pattern, source, re.MULTILINE)
    if not match:
        raise RuntimeError("Cannot extract production " + name)
    opening = match.end() - 1
    depth = 0
    for token in re.finditer(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[opening:]):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                end = opening + token.end()
                if structure:
                    if source[end] != ";":
                        raise RuntimeError("Missing production struct terminator")
                    end += 1
                body = source[match.start():end]
                return body, {"name": name, "line": source.count("\n", 0, match.start()) + 1,
                              "sha256": hashlib.sha256(body.encode()).hexdigest()}
    raise RuntimeError("Unbalanced production " + name)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root / "upstreams/AnyPS5",
                        help="Patched AnyPS5 source root")
    parser.add_argument("--output-dir", type=Path,
                        default=root / "build/thread-priority-checks/hle")
    args = parser.parse_args()
    libs = args.source.resolve() / "core/libs"
    pthread = libs / "prx/libkernel/Pthread"
    thread = pthread / "src/Thread.cpp"
    helper = pthread / "include/HostThreadPriority.hpp"
    errors = libs / "prx/libkernel/KernelErrors.hpp"
    fixtures = [root / "tools/checks" / name for name in ("host_thread_priority.cpp", "host_thread_priority_startup.cpp")]
    inputs = [thread, helper, errors, *fixtures, Path(__file__).resolve()]
    source_hashes = {str(path): digest(path) for path in inputs}
    source = thread.read_text()
    setter, setter_info = extract(source, "scePthreadSetprio")
    mutex = "static std::mutex hostPriorityLock;"
    if source.count(mutex) != 1:
        raise RuntimeError("File-local production priority mutex missing or duplicated")
    parts = [extract(source, name, structure) for name, structure in (
        ("ThreadArgs", True), ("NativeThreadArgs", True), ("StartNativeThread", False), ("scePthreadCreate", False))]
    start, create = parts[-2][0], parts[-1][0]
    if not start.index("HostThreadPriority::Apply") < start.index("args->initialized.set_value(SCE_OK)") < start.index("RunThread"):
        raise RuntimeError("Production native apply no longer precedes startup success")
    failure = create.index("if (status != SCE_OK)")
    branch = create[failure:create.index("} catch", failure)]
    positions = [branch.index(item) for item in ("start.set_value(false)", "WaitForSingleObject", "CloseHandle", "return status")]
    if positions != sorted(positions) or failure >= create.index("*thread = published") or "initialized.set_exception" not in start:
        raise RuntimeError("Production startup failure cleanup/publication semantics changed")
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / "generated_actual_setter.hpp").write_text("// Verbatim production declaration/body.\n#ifdef _WIN32\n" + mutex + "\n#endif\n" + setter + "\n")
    (out / "generated_actual_startup.hpp").write_text("// Verbatim production startup.\n" + "\n\n".join(part[0] for part in parts) + "\n")
    commands = []

    def run(label, argv, environment=None):
        result = subprocess.run(argv, cwd=out, env=environment, capture_output=True,
                                text=True, timeout=60, check=False)
        log = out / (label + ".log")
        log.write_text(result.stdout + result.stderr)
        commands.append({"label": label, "argv": argv, "exitCode": result.returncode,
                         "logSha256": digest(log)})
        if result.returncode:
            raise RuntimeError(label + " failed; see " + str(log))
        return result.stdout

    def no_winapi_imports(label, executable):
        symbols = run(label, shlex.split(os.environ.get("NM", "nm")) + ["-u", str(executable)])
        forbidden = {"SetThreadPriority", "GetLastError", "beginthreadex", "WaitForSingleObject", "CloseHandle"}
        if any(line.split() and line.split()[-1].lstrip("_") in forbidden for line in symbols.splitlines()):
            raise RuntimeError("A fixture imports a real Windows API")

    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-pedantic", "-O1", "-g", "-pthread",
             "-I", str(out), "-I", str(helper.parent), "-I", str(libs)]
    environment = dict(os.environ, APS5_HOST_THREAD_PRIORITY="1",
                       ASAN_OPTIONS="halt_on_error=1:abort_on_error=1",
                       UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    variants = {}
    for variant, extra in (("strict", []), ("asan_ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
        results = {}
        for name, fixture, fixture_flags in (
            ("mappingSetter", fixtures[0], []), ("nonWindows", fixtures[0], ["-DTEST_NON_WINDOWS=1"]),
            ("actualStartup", fixtures[1], [])):
            executable = out / (name + "-" + variant)
            run("build-" + executable.name, compiler + flags + extra + fixture_flags + [str(fixture), "-o", str(executable)])
            no_winapi_imports("imports-" + executable.name, executable)
            results[name] = json.loads(run("run-" + executable.name, [str(executable)], environment))
            if name == "mappingSetter":
                gates = []
                for index, value in enumerate((None, "", "0", "1", "01", "true", "1 ", " 1", "11")):
                    child = dict(environment)
                    child.pop("APS5_HOST_THREAD_PRIORITY", None)
                    if value is not None:
                        child["APS5_HOST_THREAD_PRIORITY"] = value
                    result = json.loads(run("gate-" + variant + "-" + str(index),
                        [str(executable), "--gate", "1" if value == "1" else "0"], child))
                    gates.append({"initialValue": value, "freshProcess": True, "result": result})
                results["cachedGates"] = gates
            if name == "actualStartup":
                child = dict(environment)
                child.pop("APS5_HOST_THREAD_PRIORITY", None)
                results["defaultOffStartup"] = json.loads(run("startup-off-" + variant, [str(executable), "--off"], child))
        variants[variant] = results
    for path, expected in source_hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError("Production source or fixture changed during checks: " + path)
    report = {"schema": 1, "status": "passed", "scope": "Actual HLE helper/setter/startup/create bodies with WinAPI mocks and minimal thread types.",
              "realWinApiCalls": False, "deviceExecuted": False, "guestExecuted": False,
              "sourceSha256": source_hashes, "commands": commands, "variants": variants,
              "extractedFunctions": [setter_info, *[part[1] for part in parts]],
              "limits": ["No real Windows/Wine priority or iPad Mach acceptance.",
                         "Priority mutex serializes requests; inherited join/detach/stale-object lifetime is not newly protected.",
                         "No full kernel link, audio, game or performance acceptance."]}
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": "passed", "report": str(out / "report.json"),
                      "reportSha256": digest(out / "report.json"), "variants": list(variants)}))


if __name__ == "__main__":
    main()
