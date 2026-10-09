# Optional legacy AudioOut frame pacing

Patch0065 gives each real legacy `sceAudioOut` port an optional submission
cadence. Enable it in a game's configuration and start a fresh app process:

```ini
env.APS5_AUDIOOUT_FRAME_PACING = 1
```

The value must be exactly `1`. The switch is cached once per library load;
unset, `0`, `true`, whitespace and other values leave the existing path active.
It is default-off and does not enable thread-priority experiments or audio
capture. Virtual ports keep their existing pacing.

## What changes

The observed port submits 256 stereo frames at 48 kHz, while SDL obtains a
1,024-frame callback. The existing byte-watermark guard permits several small
submissions after each larger callback drain. In the disabled capture, four
submissions recur together before the next wait.

The optional cadence waits **before** reading or preparing guest input. It
retains the existing SDL byte-watermark guard, conversion, volume processing,
null-buffer drain, positive frame-count return and last-output-time behavior.
The cadence advances only after successful SDL enqueue. No new worker, lock,
PCM buffer or public structure is introduced; its state belongs to the
file-private port under the existing AudioOut mutex.

The original approximately 40-ms queue target supplies a one-time startup
budget. For 256 frames at 48 kHz, the old strict watermark permits nine
successful packets, or 48 ms of source frames. A clear, drain or failure resets
the deadline without refilling that budget; any unused startup packets remain.
Closing and reopening creates a new port and budget.

After startup, integer microsecond deadlines retain the division remainder:
the 48-kHz example advances by 5,333, 5,333 and 5,334 microseconds. Small
oversleeps retain that phase. An enqueue at least one block period late
reanchors the next deadline instead of releasing a burst to catch up.
Backward clocks and deadline overflow reset the timeline. Blocks shorter than
one microsecond retain the legacy path. This is a host compatibility option,
not a claim about the original console's scheduling contract.

## Physical-device observation

Two separate 105-second launches used the same Native53 host and Source65
package. Both thread-priority gates and their trace stayed off. The only
functional switch changed was frame pacing; each launch used fresh private
shader-cache and capture directories. No XCTest, screenshot, orientation or
touch action ran during the pair. Each independent capture retained 480,000
frames after skipping 2,160,000 frames at 48 kHz.

| Captured measurement | Pacing off | Pacing on |
| --- | ---: | ---: |
| Matching raw fingerprints four 256-frame calls apart | 1,855 / 1,870 | 0 / 1,870 |
| Byte-equal adjacent complete native 1,024-frame blocks | 463 / 467 | 0 / 467 |
| Distinct complete native 1,024-frame contents | 5 / 468 | 468 / 468 |
| Longest consecutive identical native block run | 262 | 1 |
| Median adjacent successful enqueue interval | 0.0396 ms | 5.4415 ms |
| Mean adjacent successful enqueue interval | 5.3283 ms | 5.3331 ms |

With pacing on, the 1,873 qualified enqueue intervals range from 4.6999 to
5.7170 ms; none is shorter than 1 ms. The disabled capture forms 468 four-call
groups plus one two-call boundary group using a strict 1-ms gap threshold.
The enabled capture forms 1,874 single-call groups under that same threshold.

The original strict capture results remain inconclusive: concurrent log
writers split arm markers in both launches and the empty second-generation
export line in the disabled launch. A separate diagnostic analysis checks the
exact unchanged log fragments, clean main-generation and native exports,
completed structured files and actual worker closure. It does not repair logs
or turn the original checker result into a pass.

These are observed pacing and captured-pattern changes. Fingerprints do not
prove source byte identity; source and native capture clocks or phases are not
aligned. Collection briefly suspends the owned process after observation.
Listening quality, defect causality, visible gameplay, controls, saving,
simulation speed and FPS have not been accepted by this pair. Repeated or
silent audio can also legitimately repeat. The [sanitized evidence](evidence/audioout-frame-pacing-20261010.json)
preserves these limits; private PCM, game data and diagnostics remain local.
The separate device receipt verifies all 37 cleanup/restore steps, original
runtime/metadata/cache/diagnostics, Native48 reinstallation and a fresh nonce
probe that retargets the Wine prefix. This does not upgrade the original
capture-parser qualification or establish a permanent game installation.

## Reusable source checks

After applying the patch series, run the checker with a fresh output directory:

```sh
python3 -B scripts/check-audioout-frame-pacing.py --output build/audioout-frame-pacing-checks-new
```

The checker extracts the actual production port and 18 function bodies. It
reverses only patch0065 into a private baseline, leaving the supplied source
tree untouched. Strict and ASan/UBSan host fixtures compare the default-off
100-call PCM/SDL/clock/wait trace with that baseline. Separate assertions cover
conversion and queue failures, drain/clear behavior, mixed virtual/device
batches, multiple ports, duplicate handles, stalls and interrupted sleeps.
The long fixtures make 2,000 actual `sceAudioOutOutput` calls each at 48 and
44.1 kHz against a fake 1,024-frame drain and check rational deadlines,
bounded jitter and cached switches with an independent wide-integer reference.

These are host mocks, not real SDL/CVT, device audio or performance tests.
Generated source, binaries, logs and reports stay in the fresh output
directory. No GitHub Actions or device access is involved.

The published checker passes six strict/sanitizer runs and three semantic
negatives: removing the rational fraction, successful-enqueue commit or raw
hook causes the expected assertion to fail. The complete 65-patch series also
passes disposable reverse validation across 160 touched paths. That series
check is not another full HLE build or runtime test.
