#!/usr/bin/env python3
"""Check the real FrameTiming no-op and explicitly enabled paths."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = r'''
#include "prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp"
#include <cassert>
int main(int argc,char**) {
 using namespace AgcDriver;
 FrameTiming frame(1);
 assert(FrameTiming::Enabled()==(argc>1));
 if(argc==1) {
  assert(frame.Get("test","disabled")==nullptr);
  frame.Add(nullptr,{});
  frame.Print(0,0,0,{},{}); // disabled profiling must not validate diagnostic lineage
 } else {
  assert(frame.Get("test","enabled")!=nullptr);
  bool threw=false;try {frame.Print(0,0,0,{},{});}catch(const std::runtime_error&) {threw=true;}assert(threw);
  auto now=FrameTiming::Clock::now();frame.IncludeSubmission(1,now,now,now,true);frame.SetFlip(1,0,now,now);
  frame.Print(0,0,0,now,{});
 }
 PerformanceContext context(&frame);PerformanceTimer timer("test");timer.Mark("one");
}
'''
with tempfile.TemporaryDirectory(prefix="anyps5-frame-timing-") as temp:
    path = Path(temp); (path / "check.cpp").write_text(source)
    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + ["-std=c++20", "-DAPS5_ENABLE_TIMING_LOG=0", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-I" + str(root / "upstreams/AnyPS5/core/libs"), str(path / "check.cpp"), "-o", str(path / "check")], check=True)
    env = dict(os.environ);env.pop("APS5_FRAME_TIMING",None)
    subprocess.run([str(path / "check")], env=env,check=True,timeout=10)
    env["APS5_FRAME_TIMING"]="1"
    subprocess.run([str(path / "check"),"enabled"],env=env,check=True,timeout=10)
print("PASS production FrameTiming disabled path and runtime opt-in")
