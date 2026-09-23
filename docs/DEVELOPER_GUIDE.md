# foo_sacd_dlna — Developer Guide

## Source baseline

- Version file: `VERSION`
- Current tree value: `0.8-alpha3-u-dvda-flac-libflac`
- SDK: foobar2000 SDK 2025-03-07
- Windows target: x64
- Documented compiler family: MSVC v142
- UI framework used by the component: WTL/ATL as supplied/configured for the SDK tree

## Build

Open `foo_sacd_dlna.sln` in Visual Studio 2022. Configure the SDK and WTL according to `BUILD.md` and `WTL.props`.

A source edit is not considered build-validated until a real Windows/MSVC build is recorded in the corresponding validation document.

## Tests

This source package does not claim a currently validated Windows test harness. Runtime validation must be performed on Windows after building the exact tree. For DVD-Audio → FLAC, use the `Win64/flac.exe` and `metaflac.exe` tools from the matching FLAC 1.5.0 distribution when available, and record the result in `docs/VALIDATION_STATUS.md`.

## UI rules

Preferences pages must remain native `preferences_page_instance` children. Do not resize or manipulate the foobar2000 Preferences host window to compensate for control layout.

The current child dialog resources are 450×400 dialog units for Status, Settings and Maintenance.

## Concurrency

Do not replace the atomic stream admission control with a check-then-increment counter. The limit must be enforced at the point where a stream is actually admitted for transmission, and the slot must be released on every exit path.

Client accounting and stream accounting are separate concepts.

## Caching

Any generated audio cache must have a deterministic cache key and, where applicable, a manifest containing the decoder/processing state required to establish validity. Temporary files must not become visible as complete media.

## Documentation rule

When changing behavior, update:

1. `CHANGELOG.md` for the historical change record;
2. the relevant consolidated document in `docs/`;
3. `PREFERENCES_FIELDS.md` if a Preferences field changes;
4. validation documentation if the change affects build/runtime claims;
5. hardware validation documentation when renderer-specific behavior changes.


### DVD-Audio cache availability

`ensureCachedFlac()` must not use `dvda_plugin_installed()` as a hard prerequisite for serving or generating a cache. That probe enumerates `componentversion` services; it is diagnostic metadata, not the decoder API. DVD-Audio conversion itself goes through `input_entry::g_open_for_info_read()` / `input_entry::g_open_for_decoding()`. Existing validated caches remain usable when the component probe returns no version.
### Media HTTP diagnostics v4

The media endpoint distinguishes an unknown media ID (`404`) from a known item whose DVD-Audio/DSF preparation failed (`503`). Network diagnostics record the preparation reason so renderer logs can identify whether the failure is ContentDirectory/media-ID mapping or cache/decoder generation.



### DVD-Audio: PCM authoritative format

The DVD-Audio FLAC path opens the decoder before configuring libFLAC and uses the first decoded PCM chunk as the authoritative sample-rate/channel layout. This avoids relying exclusively on static `file_info` metadata for DVD-Audio program variants such as downmix and C/LFE tracks. Subsequent chunks must keep the same PCM format; a mismatch is reported as a conversion error.

### v7 Range/stream limiter behavior

A single renderer may use multiple HTTP Range connections during seeking or prefetch. These connections are treated as one logical active stream when they originate from the same already-streaming peer, so the Max Streams limit does not reject a renderer's own seek/prefetch connection with HTTP 503.

## v8 — DVD-Audio decoder priming-chunk fix

DVD-Audio tracks may emit one or more empty/setup PCM decoder runs before the first real block. The FLAC conversion path skips those runs and derives the libFLAC stream format from the first non-empty PCM block; a 64-run guard prevents an infinite loop.
