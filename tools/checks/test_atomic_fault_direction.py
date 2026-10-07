#!/usr/bin/env python3
"""Check the production ARM64 RMW classifier, including load-only exclusions.
Device access direction and page repair require the separate Windows probe.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASE = r'''
#include <assert.h>
#include "aps5_atomic_fault.h"
int main(void) {
 for (unsigned acquire=0;acquire<2;acquire++) for (unsigned release=0;release<2;release++) {
  for (unsigned size=0;size<2;size++) for (unsigned s=0;s<32;s+=2) for(unsigned t=0;t<32;t+=2) for(unsigned n=0;n<32;n++) {
   unsigned op=0x08207c00u | (size<<30) | (acquire<<22) | (release<<15) | (s<<16) | (n<<5) | t;
   assert(aps5_arm64_rmw(op)); assert(!aps5_arm64_rmw(op|1u)); assert(!aps5_arm64_rmw(op|0x10000u));
  }
  for(unsigned size=0;size<4;size++) {
   unsigned op=0x08a07c00u|(size<<30)|(acquire<<22)|(release<<15);
   assert(aps5_arm64_rmw(op));
  }
 }
 for(unsigned op=0;op<=8;op++)for(unsigned size=0;size<4;size++)for(unsigned a=0;a<2;a++)for(unsigned l=0;l<2;l++) {
  unsigned insn=0x38200000u|(op<<12)|(size<<30)|(a<<23)|(l<<22)|(2<<5)|1;
  assert(aps5_arm64_rmw(insn));
 }
 unsigned loads[]={0xf8bfc041u,0xb8bfc041u,0x38bfc041u,0x78bfc041u};
 for(unsigned i=0;i<sizeof(loads)/sizeof(loads[0]);i++)assert(!aps5_arm64_rmw(loads[i]));
 unsigned other[]={0xc85f7c20u,0xc87f0440u,0xf9400020u,0xb9400020u,0xd941c101u,0x3dc00020u,0xd503201fu,0};
 for(unsigned i=0;i<sizeof(other)/sizeof(other[0]);i++)assert(!aps5_arm64_rmw(other[i]));
 return 0;
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "upstreams/Madeira")
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    args = parser.parse_args()
    include = args.source.resolve() / "build/ntdll-unix"
    with tempfile.TemporaryDirectory(prefix="aps5-atomic-direction-") as directory:
        directory = Path(directory)
        case = directory / "case.c"
        case.write_text(CASE)
        output = directory / "case"
        subprocess.run(shlex.split(args.cc) + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I" + str(include),
            str(case), "-o", str(output)], check=True)
        subprocess.run([str(output)], check=True, timeout=10)
    print("PASS production ARM64 CAS/CASP/LSE RMW direction and load-only exclusions")

if __name__ == "__main__":
    main()
