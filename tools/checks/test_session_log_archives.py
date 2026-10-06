#!/usr/bin/env python3
"""Compile and exercise the exact Foundation archive helper from the iOS app."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'upstreams/Madeira/app/Madeira/LogStore.swift').read_text()
helper=s[s.index('enum SessionLogArchive {'):s.index('final class LogStore:')]
test=r'''
let fm = FileManager.default
let root = fm.temporaryDirectory.appendingPathComponent(UUID().uuidString)
try fm.createDirectory(at: root, withIntermediateDirectories: true)
defer { try? fm.removeItem(at: root) }
let log = root.appendingPathComponent("madeira-log.txt")
try Data([0, 1, 255]).write(to: log)
try SessionLogArchive.begin(log, name: "game-first.txt")
let first = root.appendingPathComponent("logs/game-first.txt")
assert(try fm.attributesOfItem(atPath: log.path)[.referenceCount] as? Int == 1)
assert(try fm.attributesOfItem(atPath: first.path)[.referenceCount] as? Int == 1)
let file = try FileHandle(forWritingTo: log)
try file.seekToEnd(); try file.write(contentsOf: Data([42]))
try SessionLogArchive.snapshot(log)
assert(try Data(contentsOf: first) == Data([0,1,255,42]))
// An already-open writer continues appending after an atomic archive refresh.
try file.write(contentsOf: Data([43])); try file.close()
// Relaunch seals the crash log before rotation.
try SessionLogArchive.snapshot(log, clearPending: true)
assert(try Data(contentsOf: first) == Data([0,1,255,42,43]))
assert(!fm.fileExists(atPath: SessionLogArchive.pendingURL(log).path))
try fm.moveItem(at: log, to: root.appendingPathComponent("madeira-log.prev.txt"))
try Data([99]).write(to: log)
try SessionLogArchive.begin(log, name: "game-second.txt")
assert(try Data(contentsOf: first) == Data([0,1,255,42,43]))
try "../escape.txt".write(to: SessionLogArchive.pendingURL(log), atomically: true, encoding: .utf8)
do { try SessionLogArchive.snapshot(log); fatalError("unsafe target accepted") }
catch { assert(!fm.fileExists(atPath: root.appendingPathComponent("escape.txt").path)) }
assert(try Data(contentsOf: log) == Data([99]))
print("PASS actual session archive helper: single-link export, binary bytes, open writer, finish, crash/relaunch, rotation and target rejection")
'''
# Swift assert uses nonthrowing autoclosures; evaluate throwing comparisons first.
import re
test=re.sub(r'assert\((try .*)\)',lambda m:'let check'+str(m.start())+' = '+m.group(1)+'\nassert(check'+str(m.start())+')',test)
with tempfile.TemporaryDirectory(prefix='anyps5-log-archive-') as tmp:
 src=Path(tmp)/'test.swift';src.write_text('import Foundation\n'+helper+test)
 exe=Path(tmp)/'test';cache=Path(tmp)/'module-cache'
 subprocess.run(['xcrun','swiftc','-module-cache-path',str(cache),str(src),'-o',str(exe)],check=True,env=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer'))
 subprocess.run([str(exe)],check=True)
