#!/usr/bin/env python3
"""Execute packaged guest fixtures on native Windows, retaining exact evidence.

GPU absence is a separate not_run result. A runnable GPU followed by a failing
demo is a failure, never rewritten into a skip. No device acceptance is implied.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import subprocess
import sys
import time


def package_path(package, name):
    """Resolve the manifest's portable relative names without Windows aliases."""
    if (not isinstance(name, str) or not name or "\\" in name or ":" in name or "\0" in name or
            PureWindowsPath(name).drive or PurePosixPath(name).is_absolute() or
            any(part in ("", ".", "..") for part in name.split("/"))):
        raise ValueError(f"invalid package path: {name!r}")
    path = (package / name).resolve()
    if package not in path.parents or not path.is_file():
        raise ValueError(f"package file is missing or outside the package: {name}")
    return path


def validate_package(package, metadata):
    if (not isinstance(metadata, dict) or metadata.get("schema") != 1 or
            metadata.get("kind") != "anyps5_original_runtime_package" or metadata.get("demo_profile") != "smoke"):
        raise ValueError("Windows reference runner requires a packaged smoke profile")
    files = metadata.get("files")
    if not isinstance(files, dict) or not files:
        raise ValueError("package manifest has no file hashes")
    verified = {}
    for name, wanted in files.items():
        path = package_path(package, name)
        if hashlib.sha256(path.read_bytes()).hexdigest() != wanted:
            raise ValueError(f"package file mismatch: {name}")
        verified[name] = path
    entries = metadata.get("entrypoints")
    if not isinstance(entries, dict) or not {"demo", "guest_cpu"}.issubset(entries):
        raise ValueError("package manifest is missing required entrypoints")
    entrypoints = {}
    for fixture, name in entries.items():
        path = package_path(package, name)
        if name not in verified or path.suffix.lower() != ".exe":
            raise ValueError(f"entrypoint is not a hash-verified executable: {fixture}")
        entrypoints[fixture] = path
    return entrypoints


def vulkan_inventory():
    try:
        library = ctypes.WinDLL("vulkan-1.dll")
    except OSError as error:
        missing = getattr(error, "winerror", None) == 126
        return {"status": "not_run" if missing else "fail", "reason": "vulkan_loader_unavailable" if missing else "vulkan_loader_failed",
                "detail": str(error), "devices": []}
    ptr, u32 = ctypes.c_void_p, ctypes.c_uint32

    class AppInfo(ctypes.Structure):
        _fields_ = [("type", u32), ("next", ptr), ("name", ctypes.c_char_p), ("version", u32),
                    ("engine", ctypes.c_char_p), ("engine_version", u32), ("api", u32)]

    class InstanceInfo(ctypes.Structure):
        _fields_ = [("type", u32), ("next", ptr), ("flags", u32), ("application", ctypes.POINTER(AppInfo)),
                    ("layers_count", u32), ("layers", ptr), ("extensions_count", u32), ("extensions", ptr)]

    create = library.vkCreateInstance
    create.argtypes, create.restype = [ctypes.POINTER(InstanceInfo), ptr, ctypes.POINTER(ptr)], ctypes.c_int32
    enumerate_devices = library.vkEnumeratePhysicalDevices
    enumerate_devices.argtypes, enumerate_devices.restype = [ptr, ctypes.POINTER(u32), ctypes.POINTER(ptr)], ctypes.c_int32
    properties = library.vkGetPhysicalDeviceProperties
    properties.argtypes, properties.restype = [ptr, ptr], None
    destroy = library.vkDestroyInstance
    destroy.argtypes, destroy.restype = [ptr, ptr], None
    app = AppInfo(0, None, b"AnyPS5 Windows reference", 1, b"none", 1, (1 << 22))
    info = InstanceInfo(1, None, 0, ctypes.pointer(app), 0, None, 0, None)
    instance = ptr()
    result = create(ctypes.byref(info), None, ctypes.byref(instance))
    if result:
        return {"status": "not_run" if result == -9 else "fail", "reason": "vulkan_incompatible_driver" if result == -9 else "vulkan_instance_failed",
                "vk_result": result, "devices": []}
    try:
        count = u32()
        result = enumerate_devices(instance, ctypes.byref(count), None)
        if result or count.value > 256:
            return {"status": "fail", "reason": "vulkan_enumeration_failed", "vk_result": result, "devices": []}
        devices = (ptr * count.value)()
        result = enumerate_devices(instance, ctypes.byref(count), devices) if count.value else 0
        if result:
            return {"status": "fail", "reason": "vulkan_enumeration_failed", "vk_result": result, "devices": []}
        inventory = []
        for device in devices[:count.value]:
            # Core VkPhysicalDeviceProperties begins with five uint32 values
            # and a fixed256-byte name; reserve ample space for limits below it.
            buffer = ctypes.create_string_buffer(4096)
            properties(device, buffer)
            values = (u32 * 5).from_buffer(buffer)
            inventory.append({"name": buffer.raw[20:276].split(b"\0", 1)[0].decode("utf8", "replace"),
                              "api_version": values[0], "driver_version": values[1],
                              "vendor_id": values[2], "device_id": values[3], "device_type": values[4]})
        hardware = any(device["device_type"] in (1, 2) for device in inventory)
        return {"status": "available" if hardware else "not_run",
                "reason": "integrated_or_discrete_gpu" if hardware else "no_integrated_or_discrete_vulkan_gpu",
                "devices": inventory}
    finally:
        destroy(instance, None)


def run_guest(executable, evidence, name, environment, timeout, required, probe=None, require_summary=False):
    stdout, stderr = evidence / (name + ".stdout.log"), evidence / (name + ".stderr.log")
    started = time.monotonic()
    timed_out = False
    with stdout.open("wb") as output, stderr.open("wb") as error:
        process = subprocess.Popen([str(executable)], cwd=executable.parent, env=environment, stdout=output, stderr=error)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            # Scope termination to this newly created guest process and children.
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            process.kill()
            code = process.wait(timeout=15)
    records = []
    for line in stdout.read_text(errors="replace").splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(record, dict) and record.get("schema") == 1 and (probe is None or record.get("probe") == probe):
            records.append(record)
    observed = {record.get("stage") for record in records if record.get("status") == "pass"}
    missing = sorted(set(required) - observed)
    bad = [record for record in records if record.get("status") == "fail"]
    if require_summary and not any(record.get("summary") is True and record.get("failed") == 0 for record in records):
        missing.append("successful_probe_summary")
    result = {"schema": 1, "probe": "windows_reference", "fixture": name,
              "status": "pass" if code == 0 and not timed_out and not missing and not bad else "fail",
              "exit_code": code, "timed_out": timed_out, "elapsed_seconds": round(time.monotonic() - started, 3),
              "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
              "missing_required_stages": missing, "scope": "native_windows_execution_only"}
    with (evidence / (name + ".jsonl")).open("w") as stream:
        for record in [*records, result]:
            stream.write(json.dumps(record, sort_keys=True) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--cpu-timeout", type=int, default=90)
    parser.add_argument("--demo-timeout", type=int, default=90)
    parser.add_argument("--probes-dir", type=Path, help="also run the standalone cpu, memory and allocator probes")
    args = parser.parse_args()
    if os.name != "nt":
        raise SystemExit("This reference runner requires native Windows; it does not silently substitute Wine.")
    package = args.package.resolve()
    metadata = json.loads((package / "package-manifest.json").read_text())
    entrypoints = validate_package(package, metadata)
    evidence = (args.evidence or package.parent / "windows-reference-evidence").resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.update(metadata["environment"])
    environment["PATH"] = str(package / "libs") + os.pathsep + environment.get("PATH", "")
    probes = []
    if args.probes_dir:
        for name in ("cpu", "memory", "allocator"):
            probes.append(run_guest(args.probes_dir.resolve() / (name + "-probe.exe"), evidence, "native-" + name,
                                    environment, 90, [], probe=name, require_summary=True))
        protected_write = args.probes_dir.resolve() / "protected-write-probe.exe"
        if protected_write.is_file():
            probes.append(run_guest(protected_write, evidence, "native-protected-write", environment, 90,
                                    ["allocate", "handler", "store_and_repair", "remove_handler", "release", "complete"],
                                    probe="protected-write"))
    cpu = run_guest(entrypoints["guest_cpu"], evidence, "guest-cpu", environment,
                    args.cpu_timeout, ["entry", "sysv_register_and_stack_arguments", "main_elf_tls_template",
                                       "thread_join_callback_tls_stack", "atomic_64_counter", "pthread_key_destructors",
                                       "main_tls_isolation", "complete"], probe="guest_cpu")
    exceptions = None
    if "guest_exceptions" in entrypoints:
        exceptions = run_guest(entrypoints["guest_exceptions"], evidence, "guest-exceptions", environment,
                               90, ["entry", "heap_write_read_free", "typed_catch_and_rethrow", "unwind_destructors", "complete"],
                               probe="guest_exceptions")
    gpu = vulkan_inventory()
    (evidence / "vulkan-inventory.json").write_text(json.dumps(gpu, indent=2) + "\n")
    if gpu["status"] == "available":
        demo = run_guest(entrypoints["demo"], evidence, "demo", environment,
                         args.demo_timeout, ["readback_frames", "frames_presented", "complete"], probe="demo")
    else:
        demo = {"schema": 1, "probe": "windows_reference", "fixture": "demo", "status": gpu["status"],
                "reason": gpu["reason"], "scope": "no_graphics_execution_or_acceptance"}
        (evidence / "demo-not-run.json").write_text(json.dumps(demo, indent=2) + "\n")
    summary = {"schema": 1, "cpu": cpu, "exceptions": exceptions, "native_probes": probes, "graphics": demo,
               "package_manifest_sha256": hashlib.sha256((package / "package-manifest.json").read_bytes()).hexdigest(),
               "device_acceptance": "not_run"}
    (evidence / "reference-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return int(cpu["status"] != "pass" or demo["status"] == "fail" or
               (exceptions is not None and exceptions["status"] != "pass") or any(item["status"] != "pass" for item in probes))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(f"Windows reference failed: {error}")
