# Code audit — foo_sacd_dlna 0.8 Alpha 3 T

Review of Alpha 3 S against the foobar2000 SDK 2025-03-07 sources.

## Findings that were verified against the SDK

| # | Finding | Evidence |
|---|---------|----------|
| 1 | Local files and the profile directory are `file://...` URLs, but were passed straight to `CreateDirectoryW`, `std::ifstream` and `std::filesystem` | `core_api.h` documents `get_profile_path()` as `file://c:\...`; `filesystem.cpp: extract_native_path()` strips the prefix |
| 2 | DoP -> DSF wrote the two bytes of each DoP word in reverse time order and did not reverse the bits | Test: DSD64 tone -> DoP -> plugin code -> DSF -> `ffmpeg`; SNR 25 dB (old) vs 54-69 dB (fixed); fixed output is bit-exact |
| 3 | DSF final block not padded to 4096 bytes per channel | DSF specification; `tests/dsf_dop_selftest` |
| 4 | `publish()` opened every track and used linear searches inside sort comparators, on the main thread | code; SDK offers `metadb_handle::get_info_ref()` / `get_filestats()` |
| 5 | `serveMedia()` returned `false` after an early-ended stream, making `handleClient` append a `404` | code |

| 6 | Root exposed only `Artists`; no Albums, Genres, Folders or All Tracks view | `browseDidl()` root branch |
| 7 | Every ContentDirectory request other than `GetSystemUpdateID` was answered as a `Browse`; `GetSearchCapabilities`/`GetSortCapabilities` (mandatory in ContentDirectory:1) were neither declared nor answered | dispatch in `handleClient`; SCPD |

## Checked and found correct (no change)

- `toInt24()` passes `scale = 1.0` to `audio_math::convert_to_int24`, which multiplies by `0x800000` internally (`pfc/audio_math.cpp`).
- `input_flag_dop` exists in `input.h` (`1 << 6`).

## Not changed, still open

- **Library sharing is off by default.** `cfg_bool share_library` defaults to `false`; until "Share Music Library" is enabled once (it is then
  remembered and re-applied at start-up) the server has no tracks and every container is empty. Only tracks in the foobar2000 **Media Library**
  are shared (`library_manager::get_all_items`), not playlist-only files.

- A `HEAD` request for a SACD ISO or DSP-processed item still blocks until the whole conversion is cached. A DSF's size is computable from duration and DSD rate, so `HEAD` could answer without converting.
- The concurrent-stream limit is checked before conversion but counted in `updateStreamStart()`; requests waiting on the same conversion can briefly exceed it.
- The "server buffer" figure is a byte counter, not an actual read-ahead thread.
- `ConnectionManager` answers every POST with the same `GetProtocolInfoResponse`.
- `publish()` still runs on the main thread (now without disk I/O in the normal case).
- Everything from the Alpha 3 M "Remaining observations" list.
