#!/usr/bin/env python3
"""Check production Wine priority contracts using original Mach/server mocks.

No Mach API, Wine server or device is executed. Actual setter, process and trace
bodies are extracted into a fresh build directory, then checked with sanitizers.
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


def extract(source, name):
    match = re.search(r"^(?:static\s+)?(?:int|unsigned int|void)\s+" + name +
                      r"\s*\([^;{}]*\)\s*\{", source, re.MULTILINE)
    if not match:
        raise RuntimeError("Cannot extract production function " + name)
    opening = match.end() - 1
    depth = 0
    for token in re.finditer(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[opening:]):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                end = opening + token.end()
                body = source[match.start():end]
                return body, {"name": name, "line": source.count("\n", 0, match.start()) + 1,
                              "sha256": hashlib.sha256(body.encode()).hexdigest()}, (match.start(), end)
    raise RuntimeError("Unbalanced production function " + name)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root / "upstreams/wine",
                        help="Patched Wine source root")
    parser.add_argument("--output-dir", type=Path,
                        default=root / "build/thread-priority-checks/native")
    args = parser.parse_args()
    server = args.source.resolve() / "server"
    inputs = [server / name for name in ("thread.c", "thread.h", "process.c", "ios_thread_precedence.h")]
    fixtures = [root / "tools/checks" / name for name in (
        "ios_thread_precedence.c", "ios_thread_priority_integration.c", "ios_thread_priority_process.c")]
    source_hashes = {str(path): digest(path) for path in inputs + fixtures + [Path(__file__).resolve()]}
    thread, process, helper = [path.read_text() for path in (inputs[0], inputs[2], inputs[3])]
    setter_parts = [extract(thread, name) for name in (
        "get_effective_thread_priority", "set_thread_priority", "set_thread_base_priority", "set_thread_disable_boost")]
    process_parts = [extract(process, name) for name in (
        "set_process_base_priority", "set_process_priority", "set_process_disable_boost")]
    trace_parts = [extract(source, name) for source, name in (
        (helper, "ios_precedence_enabled"), (thread, "get_mach_importance"), (thread, "ios_trace_explicit_priority"))]
    constants = re.findall(r"^#define THREAD_PRIORITY_REALTIME_(?:HIGHEST|LOWEST) [^\n]+", thread, re.MULTILINE)
    if len(constants) != 2:
        raise RuntimeError("Production realtime range constants missing")
    _, _, request_range = extract(thread, "set_thread_info")
    trace_range = trace_parts[-1][2]
    call_offsets = [match.start() for match in re.finditer(r"\bios_trace_explicit_priority\s*\(", thread)
                    if not trace_range[0] <= match.start() < trace_range[1]]
    if len(call_offsets) != 2 or not all(request_range[0] < offset < request_range[1] for offset in call_offsets):
        raise RuntimeError("Trace extends beyond the two explicit priority request sites")
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for filename, parts, prefix in (
        ("generated_actual_priority_functions.h", setter_parts, "\n".join(constants)),
        ("generated_process_functions.h", process_parts, ""),
        ("generated_trace_functions.h", trace_parts, "")):
        (out / filename).write_text("/* Verbatim production function extraction. */\n" + prefix + "\n" +
                                   "\n\n".join(part[0] for part in parts) + "\n")
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

    def no_mach_imports(label, executable):
        symbols = run(label, shlex.split(os.environ.get("NM", "nm")) + ["-u", str(executable)])
        forbidden = {"mach_task_self", "mach_task_self_", "mach_port_extract_right", "mach_port_deallocate",
                     "thread_info", "thread_policy_get", "thread_policy_set"}
        if any(line.split() and line.split()[-1].lstrip("_") in forbidden for line in symbols.splitlines()):
            raise RuntimeError("A fixture imports a real Mach scheduling API")

    environment = dict(os.environ)
    for gate in ("MADEIRA_IOS_THREAD_PRECEDENCE", "MADEIRA_IOS_THREAD_PRECEDENCE_TRACE"):
        environment.pop(gate, None)
    environment.update(ASAN_OPTIONS="halt_on_error=1:abort_on_error=1",
                       UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-pedantic", "-O1", "-g",
             "-I", str(out), "-I", str(server)]
    variants = {}
    for variant, extra in (("strict", []), ("asan_ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
        executables = {}
        for fixture in fixtures:
            executable = out / (fixture.stem + "-" + variant)
            run("build-" + executable.name, compiler + flags + extra + [str(fixture), "-o", str(executable)])
            no_mach_imports("imports-" + executable.name, executable)
            executables[fixture.stem] = executable
        helper_exe = executables["ios_thread_precedence"]
        core_gates = []
        for index, value in enumerate((None, "", "0", "1", "01", "true", "1 ", " 1", "11")):
            child = dict(environment)
            if value is not None:
                child["MADEIRA_IOS_THREAD_PRECEDENCE"] = value
            result = json.loads(run("core-" + variant + "-" + str(index),
                [str(helper_exe), "--gate", "1" if value == "1" else "0"], child))
            core_gates.append({"initialValue": value, "freshProcess": True, "result": result})
        process_exe = executables["ios_thread_priority_process"]
        trace_gates = []
        for index, (core, trace) in enumerate([("1", value) for value in (None, "0", "1", "01", "1 ", "true")] + [("0", "1"), ("01", "1")]):
            child = dict(environment, MADEIRA_IOS_THREAD_PRECEDENCE=core)
            if trace is not None:
                child["MADEIRA_IOS_THREAD_PRECEDENCE_TRACE"] = trace
            result = json.loads(run("trace-" + variant + "-" + str(index),
                [str(process_exe), "--trace", "1" if core == trace == "1" else "0"], child))
            trace_gates.append({"core": core, "trace": trace, "freshProcess": True, "result": result})
        variants[variant] = {
            "helperContract": json.loads(run("helper-" + variant, [str(helper_exe)], environment)),
            "actualSetters": json.loads(run("setters-" + variant, [str(executables["ios_thread_priority_integration"])], environment)),
            "actualProcessGuards": json.loads(run("process-" + variant, [str(process_exe)], environment)),
            "coreGates": core_gates, "traceGates": trace_gates}
    for path, expected in source_hashes.items():
        if digest(Path(path)) != expected:
            raise RuntimeError("Production source or fixture changed during checks: " + path)
    report = {"schema": 1, "status": "passed", "scope": "Production helper/extracted setter, process and trace contracts with Mach/server mocks.",
              "realMachCalls": False, "deviceExecuted": False, "nativeWineExecuted": False,
              "sourceSha256": source_hashes, "commands": commands, "variants": variants,
              "extractedFunctions": [part[1] for part in setter_parts + process_parts + trace_parts],
              "traceCallSites": [thread.count("\n", 0, offset) + 1 for offset in call_offsets],
              "limits": ["No full Wine bootstrap/destructor plumbing, retained-right hardware acceptance or scheduler/QoS proof.",
                         "No audio, game or performance acceptance."]}
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": "passed", "report": str(out / "report.json"),
                      "reportSha256": digest(out / "report.json"), "variants": list(variants)}))


if __name__ == "__main__":
    main()
