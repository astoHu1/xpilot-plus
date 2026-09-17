# AFV receive-volume balance and offline validation

The `afv_native::Client` public signatures, AFV DTOs, FSD authentication
protocol, simulator datarefs, and existing configuration keys are preserved.
`setRadioGain()` remains the user's base volume. Existing configs with no
balance key default to disabled; explicit saved choices are retained.

## Receive processing

Each decoded callsign stream has playback-owned RMS/gain history. A 20 ms
frame targets 0.12 RMS, gates signals below -50 dBFS, and bounds correction
to 0.25–4 times the original signal. Strength interpolates from no correction
to full correction. Reductions react faster than increases, with a sample
ramp across each frame. Silence is never boosted. Statistics do not trigger
configuration writes or realtime logging.

After radio effects, the actual output route applies a smoothed overlap gain
of `stream_count ^ (-0.5 * strength)`. Merged COM1/COM2 share one controller;
split output has independent left/right controllers, and headset/speaker
have separate state. Muted radios do not attenuate an audible radio.
Speech, noise, blocking tones and clicks all reach a final finite-value
limiter in [-1, 1]. It remains active when automatic balancing is disabled.
Disabling balance or selecting strength zero restores unity immediately.

Retuning and rerouting reset affected histories, preserving unrelated radios.
Disconnect/reset clears receive state. Decoded frame storage lives with the
stream instead of allocating a map for each callback. Existing compressor
scratch storage is also fixed size.

## Correctness and lifecycle

- Capture handles disabled filters and publishes atomic normalized meters.
- Device callbacks retain partial frames and honor interleaved channel strides.
- Close/uninit waits for callbacks before releasing their owners; changing
  channel width drains old devices first.
- Stream, encoder, logger and UDP state are synchronized. The wrapper joins
  its event loop before destroying the native Client and event base.
- Queued callback text and alias lists own their data. UI and simulator RX
  indicators use the same receive/power gates.
- Settings preview is temporary; Apply commits, Cancel restores. Slider
  values use one rounded integer and outside-dialog saves are debounced.

## Offline checks

```sh
cmake -S client/afv-native/tests -B /tmp/afv-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/afv-tests --parallel
ctest --test-dir /tmp/afv-tests --output-on-failure
# Separate builds: -DAFV_SANITIZER=address or -DAFV_SANITIZER=thread
```

Five native suites cover synthetic Opus decoding through RadioSimulation,
0/1/2/4/8 streams, quiet/loud speakers, merged/split/separate devices, muted
radios, frequency/reset recovery, strength bounds, rapid settings changes,
effects limiting, arbitrary callback lengths, missing sources and concurrent
shutdown. Device and Client lifecycle tests use fake devices; UDP concurrency
tests use localhost only. Tests never authenticate to VATSIM or launch xPilot
or X-Plane. Native public API compatibility is also checked by compiling the
full client against the updated native implementation.

The wrapper suite compiles actual wrapper/config code with audio, simulator
and network doubles and loads invisible QML. Its filesystem is disposable.
See `client/tests/afv-wrapper/README.md`. Packaging tests inspect synthetic
Mach-O fixtures without executing them; see `scripts/PACKAGING.md`.

Local validation: all five native suites pass Debug, AddressSanitizer plus
UndefinedBehaviorSanitizer, and ThreadSanitizer with AppleClang 21. The full
macOS arm64 Debug client builds. AppleClang 17's ASan fails during runtime
initialization on this macOS 27 host, so sanitizer validation uses Xcode beta's
matching runtime. macOS does not support LeakSanitizer; Linux CI enables it.
Bundled precompiled dependencies are not themselves sanitizer-instrumented.

## Remaining boundaries

This is not a live audio-quality, driver/hotplug, simulator or VATSIM validation.
The existing device-name configuration is retained; stable device identifiers
and duplicate-name migration require a separate configuration design.
Existing radio/stream mutexes and codec allocations remain; the audio engine
is not fully lock-free. Receive metrics are inspectable internal snapshots,
not a new public telemetry API.

The public auth library remains a nonfunctional development stub. Release
jobs now fail unless the approved private library and release credentials are
configured; no credentials or private authentication implementation are added.
The AFV submodule remains pinned to an exact commit in the existing fork.
Local Homebrew Qt deployment exposes missing split-module runtime paths, which
the new static verifier rejects; CI uses the complete pinned Qt 6.5.2 package.
