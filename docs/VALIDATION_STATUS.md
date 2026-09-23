# foo_sacd_dlna — Validation Status

## Current tree

`VERSION` reports:

```text
0.8-alpha3-u-dvda-flac-libflac
```

## What the historical record establishes

The consolidated project history records a successful Windows Debug x64 build and runtime confirmation for an earlier **Alpha 3 I** revision, using the foobar2000 SDK 2025-03-07, MSVC v142 and the documented WTL/pfc configuration. The current tree contains later source changes and therefore requires its own fresh Windows/MSVC v142 build.

## What is not claimed

This document does **not** claim that the supplied `0.8-alpha3-u-dvda-flac-libflac` tree has been compiled successfully in the current environment.

It also does not claim that DVD-Audio FLAC playback, the current 450×400 Preferences layout, the current stream-limit implementation, or exact T+A SDX 3100 HV firmware behavior have been hardware-validated merely because the corresponding source code exists.

## Source-level validation performed for this revision

- The FLAC validation logic was exercised against a reference FLAC 1.5.0 file containing STREAMINFO, SEEKTABLE, VORBIS_COMMENT and PADDING metadata; `flac -t` reported the file as valid and the validator logic extracted 96 kHz / 2 channels / 24-bit / 9600 samples and detected the first frame sync.
- This test validates the generic FLAC parsing assumptions only. It is **not** a Windows/MSVC component build and does not prove DVD-Audio runtime playback.

## Required validation before release

- [ ] Windows/MSVC v142 build of the exact current tree.
- [ ] Component load in foobar2000 x64.
- [ ] Preferences Status/Settings/Maintenance pages render correctly.
- [ ] SACD ISO → DSF playback.
- [ ] DSF/DFF playback.
- [ ] DVD-Audio → FLAC playback with `foo_input_dvda`.
- [ ] Generated FLAC passes `flac.exe -t` from the matching FLAC 1.5.0 Win64 package.
- [ ] `metaflac.exe --list` reports the expected sample rate, channels, bits per sample and total samples.
- [ ] Existing DVD-Audio cache is invalidated by `cacheVersion = 4`.
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
