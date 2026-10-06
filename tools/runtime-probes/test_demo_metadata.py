#!/usr/bin/env python3
"""Feed packaged demo metadata to the real libkernel parser used by VideoOut."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("package_runtime", ROOT / "scripts/package-runtime.py")
packaging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packaging)

with tempfile.TemporaryDirectory(prefix="anyps5-demo-metadata-") as directory:
    work = Path(directory)
    packaging.write_demo_assets(work / "app0")
    driver = work / "check.cpp"
    driver.write_text('''#include "prx/libkernel/AppMetadata/include/ParamJsonParser.hpp"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 2) return 64;
    const auto result = parseParamJson(argv[1]);
    if (result.titleId != "ANYPS5DEMO" || result.title != "AnyPS5 original RDNA demo" ||
        result.titleId.size() >= 12 || result.title.size() >= 128 || result.downloadDataSizeMiB != 0) return 1;
    std::cout << result.title << " metadata accepted by actual libkernel parser\\n";
}
''')
    executable = work / ("check.exe" if os.name == "nt" else "check")
    subprocess.run([os.environ.get("HOST_CXX", "c++"), "-std=c++20",
                    "-I" + str(ROOT / "upstreams/AnyPS5/core/libs"), str(driver),
                    str(ROOT / "upstreams/AnyPS5/core/libs/prx/libkernel/AppMetadata/src/ParamJsonParser.cpp"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable), str(work / "app0/sce_sys/param.json")], check=True)
