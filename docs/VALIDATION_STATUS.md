# foo_sacd_dlna — Validation Status

## Current tree

`VERSION` reports:

```text
0.8-alpha3-u-dvda-flac
```

## What the historical record establishes

The project history records a successful Windows Debug x64 build and runtime confirmation for **Alpha 3 I**, using the foobar2000 SDK 2025-03-07, MSVC v142 and the documented WTL/pfc configuration.

Later Alpha 3 revisions introduced additional source changes. Their historical validation notes repeatedly state that a fresh Windows/MSVC v142 rebuild is required before those revisions are declared build-validated.

## What is not claimed

This document does **not** claim that the supplied `0.8-alpha3-u-dvda-flac` tree has been compiled successfully in the current environment.

It also does not claim that DVD-Audio FLAC playback, the current 450×400 Preferences layout, the current stream-limit implementation, or exact T+A SDX 3100 HV firmware behavior have been hardware-validated merely because the corresponding source code exists.

## Required validation before release

- [ ] Windows/MSVC v142 build of the exact current tree.
- [ ] Component load in foobar2000 x64.
- [ ] Preferences Status/Settings/Maintenance pages render correctly.
- [ ] SACD ISO → DSF playback.
- [ ] DSF/DFF playback.
- [ ] DVD-Audio → FLAC playback with `foo_input_dvda`.
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
