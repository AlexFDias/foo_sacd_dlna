# foo_sacd_dlna — Validation Status

## Current tree

`VERSION` reports:

```text
1.0.0
```

## What the historical record establishes

The consolidated project history records a successful Windows Debug x64 build and runtime confirmation for an earlier **Alpha 3 I** revision, using the foobar2000 SDK 2025-03-07, MSVC v142 and the documented WTL/pfc configuration. The current tree contains many later source changes (through the `v8` priming-chunk fix, the `v9` `SO_SNDTIMEO` fix, and the `v10` startup libFLAC check added here) and therefore requires its own fresh Windows/MSVC v142 build before it can be called build-validated.

## What is not claimed

This document does **not** claim that the supplied `1.0.0` tree has been compiled successfully in the current environment — there is no Windows/MSVC toolchain available here.

It also does not claim that the current 450×400 Preferences layout, the current stream-limit implementation, or exact T+A SDX 3100 HV firmware behavior have been hardware-validated merely because the corresponding source code exists.

## Source-level validation performed for this revision

- The FLAC validation logic was exercised against a reference FLAC 1.5.0 file containing STREAMINFO, SEEKTABLE, VORBIS_COMMENT and PADDING metadata; `flac -t` reported the file as valid and the validator logic extracted 96 kHz / 2 channels / 24-bit / 9600 samples and detected the first frame sync.
- This test validates the generic FLAC parsing assumptions only. It is **not** a Windows/MSVC component build and does not prove DVD-Audio runtime playback.

## Real-world diagnostic evidence (not a substitute for hardware validation)

A user test session against a real foobar2000 instance, a T+A hardware DLNA renderer and VLC 3.0.24 produced two artifacts that were analyzed line-by-line: a rotating `network.log` (spanning several days and dozens of server restarts) and a VLC debug diagnostic dump.

What this evidence shows, specifically for the **most recent** server session in that log:

- Every one of the 54 DVD-Audio → FLAC conversion attempts in that session failed with exactly one message: `DVD-A FLAC conversion failed: libFLAC.dll 1.5.x was not found next to foo_sacd_dlna`.
- No occurrence of `generated FLAC failed structural validation` (the metadata-chain FLAC validator) in that session.
- No occurrence of `stream failed: socket send failed` beyond 3 residual events, consistent with the documented, intentional behavior of `SO_SNDTIMEO` failing a stuck `send()` promptly on an abandoned connection rather than indicating a bug.
- The VLC diagnostic's HTTP 503 responses correlate exactly with the same root cause: `serveMedia()` cannot produce a cache to serve, so it answers `503`, which VLC surfaces as a generic playback/format error.

Older, no-longer-current parts of the same multi-day log also show a `generated FLAC failed structural validation` failure mode from an earlier revision of `validateFlacFile()` that assumed the old hand-written FLAC writer's fixed verbatim-frame layout. That validator has since been rewritten (see `FLAC_RUNTIME.md` and `docs/ARCHITECTURE.md`) to walk the FLAC metadata block chain instead of assuming a specific frame layout, and does not appear as a failure cause in the most recent session.

**Conclusion from this evidence:** in this one test environment, the DVD-Audio FLAC encoding and cache-validation code path is not what was blocking playback. The blocker was a missing runtime file (`libFLAC.dll`) in the deployed component folder — an installation step, not a source defect. This is evidence from a single environment and log, not a hardware-validation pass; the checklist below still applies in full.

## Required validation before release

- [ ] Windows/MSVC v142 build of the exact current tree.
- [ ] Component load in foobar2000 x64.
- [ ] Preferences Status/Settings/Maintenance pages render correctly.
- [ ] SACD ISO → DSF playback.
- [ ] DSF/DFF playback.
- [ ] `libFLAC.dll` copied next to the installed `foo_sacd_dlna.dll`; Console shows no "libFLAC.dll was not found" warning at startup.
- [ ] DVD-Audio → FLAC playback with `foo_input_dvda`, with the DLL actually present.
- [ ] Generated FLAC passes `flac.exe -t` from the matching FLAC 1.5.0 Win64 package.
- [ ] `metaflac.exe --list` reports the expected sample rate, channels, bits per sample and total samples.
- [ ] Existing DVD-Audio cache is invalidated by the current `cacheVersion`.
- [ ] Shared formats behavior.
- [ ] Concurrent stream limit under contention.
- [ ] SSDP discovery from a second LAN device.
- [ ] Browse and BrowseMetadata.
- [ ] HTTP Range/seek.
- [ ] Album art.
- [ ] SACD/DSP/DVD-A cache invalidation.
- [ ] Exact T+A SDX 3100 HV firmware matrix.
- [ ] Long DSD256 playback.
- [ ] Gapless behavior recorded as PASS/FAIL, never assumed.
