#!/usr/bin/env python3
"""Validate native offscreen device evidence; never awards gameplay acceptance."""
import argparse
import json
from pathlib import Path


def validate(path, build_manifest):
    records = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    required = {"hardware_device", "VK_KHR_buffer_device_address", "VK_KHR_8bit_storage",
                "VK_KHR_shader_float_controls", "bufferDeviceAddress", "storageBuffer8BitAccess",
                "shaderInt64", "textureCompressionBC", "vertexPipelineStoresAndAtomics",
                "fragmentStoresAndAtomics", "samplerAnisotropy", "shaderSignedZeroInfNanPreserveFloat32",
                "device_creation", "bda_8bit_readback", "bc1_sample_readback", "offscreen_complete"}
    stages = {}
    for record in records:
        if record.get("schema") != 1 or record.get("stage") != "native_gpu":
            raise ValueError("unexpected report schema or stage")
        name = record.get("test")
        if name:
            if name in stages:
                raise ValueError("duplicate test: " + name)
            stages[name] = record
        if record.get("status") == "fail":
            raise ValueError("failed GPU test: " + str(name))
    for name in sorted(required):
        if stages.get(name, {}).get("status") != "pass":
            raise ValueError("missing GPU pass: " + name)
    initial, final = stages.get("device_context", {}), stages.get("foreground_completion", {})
    for label, state in (("start", initial), ("completion", final)):
        if state.get("application_state") != 0 or state.get("scene_activation_state") != 0:
            raise ValueError("application and scene must both be foreground-active at " + label)
    if final.get("interrupted") is not False or final.get("gpu_exit_code") != 0 or not final.get("native_offscreen_accepted"):
        raise ValueError("interrupted or incomplete native GPU execution")
    if not initial.get("timestamp", 0) < final.get("timestamp", 0):
        raise ValueError("invalid measurement timestamps")
    if initial.get("build_manifest") != json.loads(build_manifest.read_text()):
        raise ValueError("device report does not identify the expected build")
    devices = [r for r in records if "device" in r]
    if len(devices) != 1 or devices[0].get("device_type") not in (1, 2):
        raise ValueError("expected a single integrated or discrete hardware GPU")
    if not str(initial.get("model", "")).startswith("iPad"):
        raise ValueError("missing physical iPad model identifier")
    return {"schema": 1, "status": "pass", "scope": "native_offscreen_gpu_only",
            "model": initial["model"], "os": initial.get("os"), "gpu": devices[0]["device"],
            "elapsed_seconds": final["timestamp"] - initial["timestamp"],
            "application_and_scene_active": True, "interrupted": False,
            "wine_fex_verified": False, "visible_gameplay_verified": False,
            "full_device_acceptance": "not_run"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--build-manifest", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = validate(args.report, args.build_manifest)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(json.dumps({"schema": 1, "status": "fail", "scope": "native_offscreen_gpu_only", "reason": str(error)}))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
