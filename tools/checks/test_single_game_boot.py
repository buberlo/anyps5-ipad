#!/usr/bin/env python3
"""Compile the production Swift boot transaction with deferred dependency fakes.

macOS host test: no claim about VPN, signing, JIT execution or iPad rendering.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'upstreams/Madeira/app/Madeira/ContentView.swift').read_text()
a = source.index('// BEGIN ANYPS5 SINGLE GAME BOOT')
b = source.index('// END ANYPS5 SINGLE GAME BOOT', a)
production = source[a:b]
harness = r'''
import Foundation
import Combine
'''
harness += production
harness += r'''
@MainActor final class Fixture {
 var active = true, ready = false, preflightFails = false
 var launches = 0, connects = 0, enables = 0, probes = 0, waits = 0
 var probe: ((Bool) -> Void)?, wait: ((Bool) -> Void)?
 var connect: ((Result<Void, Error>) -> Void)?
 var enable: ((Result<Void, Error>) -> Void)?
 lazy var boot = SingleGameBoot(.init(
  active: { self.active }, jitReady: { self.ready },
  preflight: { if self.preflightFails { throw NSError(domain:"test",code:1) } },
  probe: { self.probes += 1; self.probe = $0 },
  connect: { self.connects += 1; self.connect = $0 },
  waitForConnection: { self.waits += 1; self.wait = $0 },
  enable: { self.enables += 1; self.enable = $0 }, launch: { self.launches += 1 }
 ))
 func isFailed() -> Bool { if case .failed = boot.phase { return true }; return false }
}
@main struct Tests {
 @MainActor static func main() {
  do {
   let f = Fixture(); f.ready = true; f.boot.start(); f.boot.start(); f.boot.becameActive()
   assert(f.launches == 1 && f.probes == 0 && f.enables == 0)
   f.boot.rendered(); f.boot.becameActive(); assert(f.boot.phase == .running && f.launches == 1)
  }
  do {
   let f = Fixture(); f.active = false; f.boot.start(); assert(f.probes == 0)
   f.active = true; f.boot.becameActive(); f.probe?(true); assert(f.enables == 1)
   f.ready = true; f.enable?(.success(())); f.enable?(.success(()))
   assert(f.launches == 1); f.boot.becameActive(); assert(f.launches == 1)
  }
  do {
   let f = Fixture(); f.boot.start(); f.probe?(false); assert(f.connects == 1 && f.launches == 0)
   f.connect?(.success(())); assert(f.boot.phase == .checking && f.enables == 0)
   f.boot.becameActive(); f.boot.becameActive(); assert(f.waits == 1)
   f.wait?(true); assert(f.enables == 1); f.ready = true; f.enable?(.success(()))
   assert(f.launches == 1)
  }
  do {
   let f = Fixture(); f.boot.start(); f.probe?(false); f.connect?(.failure(NSError(domain:"vpn",code:1)))
   assert(f.isFailed() && f.enables == 0 && f.launches == 0)
   let stale = f.connect; f.boot.retry(); stale?(.failure(NSError(domain:"vpn",code:1)))
   assert(f.boot.phase == .checking); f.probe?(true); f.ready = true; f.enable?(.success(()))
   assert(f.launches == 1)
  }
  do {
   let f = Fixture(); f.boot.start(); f.probe?(false); f.connect?(.success(())); f.boot.becameActive(); f.wait?(false)
   assert(f.isFailed() && f.enables == 0 && f.launches == 0)
  }
  do {
   let f = Fixture(); f.boot.start(); f.probe?(true); f.enable?(.success(()))
   assert(f.isFailed() && f.launches == 0) // a helper response cannot replace readiness
  }
  do {
   let f = Fixture(); f.boot.start(); f.probe?(true); f.enable?(.failure(NSError(domain:"test",code:2)))
   assert(f.isFailed() && f.launches == 0)
   let stale = f.enable; f.boot.retry(); f.probe?(true); stale?(.failure(NSError(domain:"old",code:3)))
   assert(f.boot.phase == .enablingJIT); f.ready = true; f.enable?(.success(())); assert(f.launches == 1)
  }
  do {
   let f = Fixture(); f.boot.start(); f.probe?(true); f.active = false; f.ready = true; f.enable?(.success(()))
   assert(f.boot.phase == .ready && f.launches == 0)
   f.active = true; f.boot.becameActive(); assert(f.launches == 1)
  }
  do {
   let f = Fixture(); f.boot.start(); f.active = false; f.probe?(true)
   assert(f.enables == 0 && f.connects == 0)
   f.active = true; f.boot.becameActive(); assert(f.enables == 1 && f.connects == 0)
  }
  do {
   let f = Fixture(); f.boot.start(); f.active = false; f.probe?(false)
   assert(f.connects == 0); f.active = true; f.boot.becameActive(); assert(f.connects == 1)
  }
  do {
   let f = Fixture(); f.preflightFails = true; f.boot.start()
   assert(f.isFailed() && f.probes == 0 && f.connects == 0 && f.launches == 0)
  }
  do {
   let f = Fixture(); f.ready = true; f.boot.start(); f.boot.rendered()
   f.ready = false; f.boot.becameActive(); f.probe?(false); f.connect?(.success(()))
   f.boot.becameActive(); f.wait?(true); f.ready = true; f.enable?(.success(()))
   assert(f.boot.phase == .running && f.launches == 1) // recover JIT without a second Wine session
   f.boot.stopped("game exited"); f.boot.retry(); f.boot.becameActive()
   assert(f.isFailed() && f.launches == 1)
  }
  do {
   let f = Fixture(); f.boot.start(); f.probe?(false)
   // Re-activation while iOS is connecting cannot race its permission callback.
   f.boot.becameActive(); assert(f.waits == 0 && f.boot.phase == .connecting)
   f.active = false; f.connect?(.success(())); assert(f.waits == 0)
   f.active = true; f.boot.becameActive(); assert(f.waits == 1)
   f.connect?(.failure(NSError(domain:"duplicate",code:2))); assert(!f.isFailed())
   f.wait?(true); f.ready = true; f.enable?(.success(())); assert(f.launches == 1)
  }
  print("PASS 13 production boot scenarios: automatic connection, readiness, lifecycle, stale callbacks and single launch")
 }
}
'''
with tempfile.TemporaryDirectory(prefix='anyps5-single-game-') as temp:
    path = Path(temp)
    (path / 'boot.swift').write_text(harness)
    subprocess.run(['xcrun', 'swiftc', '-swift-version', '5', '-parse-as-library',
                    str(path / 'boot.swift'), '-o', str(path / 'boot')], check=True)
    subprocess.run([str(path / 'boot')], check=True, timeout=10)
