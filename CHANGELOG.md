## v8 — DVD-Audio decoder priming-chunk fix

- DVD-Audio conversion now skips empty/format-only PCM decoder runs and uses the first non-empty PCM block to configure libFLAC.
- This targets the `DVD-Audio decoder returned an empty/invalid PCM chunk` failure seen on multichannel/C-LFE tracks.
- A guard of 64 consecutive empty decoder runs prevents an infinite loop while still allowing normal priming/setup runs.
- Corrected the SACD cache failure log label (`SACD DSF cache FAILED`).

## v7 — logical stream limiter / Range seek fix

- HTTP Range connections from a renderer that already has an active logical stream no longer consume an additional Max Streams slot.
- Prevents VLC/renderer seek and prefetch requests from receiving `503 Service Unavailable` merely because the renderer already owns a stream.
- Keeps the configured stream limit meaningful at the client/logical-stream level.
- DVD-Audio PCM/libFLAC path from v6 is preserved unchanged.

## DVD-Audio PCM-authoritative FLAC encoding

- The DVD-Audio decoder is now opened and the first real PCM chunk is decoded before configuring libFLAC.
- Sample rate and channel count are taken from the actual decoder output rather than relying only on `file_info` metadata.
- This handles DVD-Audio program variants where metadata and decoded PCM layout differ, including downmix and C/LFE variants.
- Every subsequent PCM chunk is checked for stable sample rate/channel count and failures include the expected and actual format.
- The first decoded chunk is retained and encoded, so no PCM samples are lost while determining the FLAC stream parameters.

## DVD-Audio decoder seek/diagnostic fix v5

- DVD-Audio decoding no longer initializes `input_decoder` with `input_flag_no_seeking`; the source decoder may seek internally for DVD navigation/program data.
- HTTP 503 responses now include the stored conversion error detail.
- Server diagnostics log the conversion detail when a DVD-Audio cache job fails.

## Media-serving diagnostics v4

- DVD-Audio/cache preparation failures no longer collapse into an indistinguishable HTTP 404.
- A known media item whose output cannot currently be prepared now returns HTTP 503 with `Retry-After: 1`.
- Network logging records `ITEM_NOT_FOUND`, `DVDA_CACHE_FAILED`, `OUTPUT_FILE_OPEN_FAILED`, and empty-output failures separately.
- This separates stale/unknown DIDL media IDs from DVD-Audio decoder/cache failures during VLC and renderer diagnostics.

## DVD-Audio decoder probe / cache serving fix

- Removed the hard `dvda_plugin_installed()` gate from `ensureCachedFlac()`. The actual decoder path already opens the source through foobar2000's `input_entry` services, so component-version enumeration is no longer treated as proof of decoder availability.
- A valid existing FLAC cache can now be served even when component-version probing temporarily returns no result.
- When probing does not find the component, the server logs that it will attempt cache/decode directly; an actual decode failure is reported as the conversion error instead of becoming a silent generic 404.
- Cache manifests use the detected `foo_input_dvda` version when available, or `unknown` when the probe is unavailable.
- The supplied `foo_input_dvda` 0.8.1 source identifies the component as `foo_input_dvda.dll` / `DVD-Audio Decoder`, which is compatible with the filename-based probe; the runtime path no longer depends on that probe for playback.
- Cache format remains 4 because this change does not alter the FLAC file format.

## Stable media IDs v2

- Media URLs now use deterministic IDs derived from source path + subsong.
- IDs survive foobar2000/component restarts instead of relying on `m_nextId`.
- This prevents stale VLC/DLNA DIDL resource URLs from becoming HTTP 404 after a restart.
- DVD-Audio FLAC serving continues to use the official libFLAC runtime and cache path.


## 0.8-alpha3-u-dvda-flac — DLNA FLAC compatibility fix

- Added FLAC to `ConnectionManager::GetProtocolInfo` Source.
- Added `DLNA.ORG_PN=FLAC` to FLAC HTTP `contentFeatures.dlna.org`.
- Kept DIDL-Lite and HTTP FLAC profile metadata consistent.
- Preserved Range/206 and application read-ahead handling.

# Current tree — 0.8-alpha3-u-dvda-flac-libflac

- Replaced the hand-written DVD-Audio FLAC frame writer with the official Xiph libFLAC 1.5.x runtime encoder.
- Removed frame-level FLAC validation that assumed the old writer's fixed-block/verbatim layout. Validation now checks the native `fLaC` signature, metadata chain, STREAMINFO and frame start without duplicating FLAC encoding logic.
- Bumped DVD-Audio cache format from 3 to 4 so caches produced by the removed writer are not reused.
- Failed/partial DVD-Audio FLAC conversions are cleaned up before the cache is published.
- Added the Win64 `libFLAC.dll` runtime to the project and made the Visual Studio build copy it to the component output directory.
- Removed obsolete per-revision build/audit/release notes from the source package and consolidated the current documentation.


## Superseded — previous DVD-Audio FLAC writer

The earlier hand-written FLAC frame fixes and cache format 3 are superseded by the libFLAC 1.5.x integration at the top of this changelog. Those frame-level details are retained here only as historical context; the current source no longer contains that writer or its assumptions.


# Alpha 3 V — client counters, stream limit, performance

Applied on top of Alpha 3 U. Not compiled here (no MSVC); the new logic was tested outside Windows (see **Tests**).

**New**
- **Status: clients.** The live status (status UI element, Preferences page and the `/status` web page) shows `total`, `active` and `idle` clients plus clients seen since start. `total = active + idle`. A client is a remote peer IP that sent HTTP requests; the built-in self-test (loopback / this machine) is not counted. `active` = is being sent audio right now; `idle` = known, not streaming, forgotten after 10 minutes of silence. `/status` also lists every client with its state, streams served, requests, silence and `User-Agent`.
- **Max streams option** (Preferences, next to HTTP port; 1-16, default 2). The slot is taken atomically when audio is about to be sent, after any SACD/DSP conversion, and released on every exit path (the old check compared a counter that only moved later, so several simultaneous requests all passed). Requests that are merely waiting for a conversion, for example ones abandoned when the user skips tracks, never hold a slot. It is re-read on every request: changes apply immediately, running streams are never cut. Requests over the limit get `503` + `Retry-After`, are counted (`rejected` in the status) and logged.

**Performance**
- Playlist matching in `publish()` used up to three full scans of the shared library, each with a Unicode-aware `path_compare`, for every playlist entry, on the main thread. It now looks candidates up in a path index and still lets `path_compare` decide (verified identical on 160,000 randomised matches).
- The `Shared formats` filter was re-parsed (two string copies + tokenising) for every track; it is now parsed once per `publish()` / `share_music_library()` (identical result on 419,000 comparisons, except that an empty extension is no longer shared because of a stray comma). Tabs and newlines also separate formats now.
- Media, artwork and prefetch lookups by id used linear scans of the whole library; they use the existing hash indexes (`m_itemIndex`, `m_albumIndex`).

**Fixed**
- The Preferences dialog had "Enable stability mode" drawn on top of the shared-formats hint text; it now sits inside its group box. The dialog grew by 15 dialog units to make room for the new rows.
- `/status` said "DSD tracks" for the shared count although other formats can be shared.

**Tests**
- `tests/client_registry_selftest/`: 53 checks on the client registry and the stream limiter, including 16 threads contending for limits 1/2/3/8 (the number of simultaneous streams never exceeded the limit); also run clean under ThreadSanitizer.
- `tests/shared_formats_selftest/`: unit test of the real `parseSharedFormats()`.
- `tests/browse_tree_selftest/` updated for playlists (16,000+ checks); it had stopped compiling after the Playlists change.

**Notes**
- The zip drops Unix execute bits: run the tests with `sh tests/<name>/run.sh`.
- Version strings (`VERSION`, `kVersion`, `main.cpp`) were already inconsistent and were left alone.

# Alpha 3 U — external code review (bug fixes + documentation)

Requested: search for bugs, optimize, update documentation. Reviewed by an AI assistant with no local Windows/MSVC toolchain (same constraint as every other change in this history) — findings below are from static reading only, not a compiled/run test.

**Investigated, found already correct (no change needed)**
- SACD ISO is already advertised to renderers purely as DSD/`.dsf` (`<dc:format>audio/dsd</dc:format>`, `servedExt` forced to `.dsf`); the raw `.iso` container is never sent. This was already right.
- Sharing non-DSD formats already works end-to-end: the `Shared formats` preferences field (`shared_formats` config, default `dsf,dff,iso`) already gates `publish()`/`share_music_library()`, is already documented via a dialog hint string and a `m_tips.add(...)` tooltip, and `serveMedia()` already streams non-DSD, non-cached files natively (no DSD Processor required) via `nativePathFromFb2k`. What was actually missing was documentation outside the dialog itself — see below.

**Fixed**
- `updateStreamStart()`/`updateStreamEnd()`: with more than one concurrent HTTP stream (the server already supports up to `kMaxConcurrentStreams`), the "now playing" Status fields (`m_streamTitle`, `m_clientIp`, etc.) were overwritten on *every* stream start, not just the first — so with 2+ simultaneous clients the Status popup would flicker to whichever client connected most recently, and could show stale info once that client disconnected while an earlier stream was still running. Now these fields are only set on the 0→1 transition, so Status consistently reflects the longest-running active stream instead of flickering. A true per-connection breakdown is still future work.
- `share_music_library()`'s console log always said "N DSD tracks", which becomes inaccurate once `Shared formats` includes non-DSD extensions. Now logs the actual count together with the active filter string.

**Documentation**
- `PREFERENCES_FIELDS.md`: added the missing `Shared formats` row (existed in the dialog and its tooltip, but not in this reference table).
- `EXAMPLES.md`: added "8. Sharing non-DSD formats (FLAC, WAV, MP3, ...)" with the exact steps and what happens with/without DSD Processor.

**Not changed**
- No further concurrency/architecture audit was done beyond the item above (`dlna_server.cpp` is ~2500 lines); treat this as a partial pass, not a full audit. None of this has been compiled or run.



Applied on top of Alpha 3 S. See `CHANGELOG.md`.

**New: complete browse tree**
- The root used to expose only `Artists`. It now exposes `Artists`, `Albums`, `Genres`, `Folders` and `All Tracks`:
  `Artists -> artist -> album -> tracks`, `Albums -> album -> tracks` (album artist shown as `dc:creator`), `Genres -> genre -> tracks`,
  `Folders -> folder -> (sub-folders and tracks)` mirroring the file system (single-child chains such as `D:\Music\DSD` are skipped),
  `All Tracks` sorted by title. Grouping, sorting and object ids live in the new `library_index.h`.
- ContentDirectory: `GetSearchCapabilities` and `GetSortCapabilities` are declared in the SCPD and answered (empty lists). Before, every
  request that was not `GetSystemUpdateID` was answered as a `Browse`, so control points calling these mandatory actions received a
  `BrowseResponse` and many gave up. Unknown actions now get UPnP error 401 instead of a Browse.

**Correctness**
- foobar2000 paths (`file://D:\Music\a.dsf`, and `core_api::get_profile_path()`) are converted to native Windows paths with the SDK's `foobar2000_io::extract_native_path` before any disk access. Previously the cache folder, `network.log`, native DSF/DFF streaming and the artwork cache were handed a `file://...` string.
- DoP -> DSF: bytes are written in time order (older byte first) and every byte is bit-reversed, as required by a DSF declaring "bits per sample = 1" (LSB first). The previous order scrambled the noise-shaped quantisation noise: in-band SNR of a decoded test signal dropped by 29-44 dB.
- DSF: the last partial block of each channel is zero-padded to a full 4096-byte block, as the format requires; write failures are detected instead of producing a silently truncated file.
- `serveMedia()` always returns `true` once response headers are sent, so `handleClient` no longer appends a `404` to a half-sent audio stream.
- Browse: `StartingIndex + RequestedCount` can no longer wrap around 32 bits (some renderers send `0xFFFFFFFF`).
- Help dialog showed literal `\n` sequences.

**Performance**
- `publish()` reads tags and file statistics from the metadb cache (`get_info_ref`, `get_filestats`) instead of opening and `stat`-ing every file on the main thread; grouping and sorting use hash lookups (the old code was O(items x albums) and O(items^2) in the sorts).
- Browse builds its response from the shared library under the lock instead of copying every item/artist/album per request; `BrowseResponse` reads `UpdateID` directly instead of building a full status snapshot.
- Component probes (`sacd_plugin_installed`, `dsd_processor_installed`) are cached after start-up, the library callback no longer builds a status snapshot per event, and DoP unpacking converts a whole chunk per call instead of one call per sample.
- Finished HTTP client and prefetch `std::thread` objects are joined and released instead of accumulating until the server stops.

**Behaviour change**
- Albums are grouped under the album artist when tagged (falling back to the track artist), so a compilation is no longer split into one pseudo-album per track artist. Tracks inside an album are ordered by disc, track number, title.

**Tests**
- `tests/dsf_dop_selftest/`: bit-exact + `ffmpeg` decode validation of the DoP -> DSF path using the plugin's own code.
- `tests/library_index_selftest/`: unit test of the artist/album/genre/folder index (74 checks).
- `tests/browse_tree_selftest/`: compiles the plugin's real `browseDidl()` and `soapActionName()` and crawls the whole tree like a renderer (16,000+ checks: well-formed DIDL-Lite, `childCount` == `TotalMatches`, `parentID` links, paging, every view reaches every track).

**Build status:** not compiled here (no MSVC). Every SDK symbol used by the new code was checked against `SDK-2025-03-07`; a Windows/MSVC v142 rebuild is required.

# Alpha 3 M — code hardening

Alpha 3 M source-level hardening:
- Lifecycle-safe start/stop recovery for HTTP/SSDP workers and Winsock state.
- Correct HTTP 503 handling before stream headers are sent.
- Exact cache-manifest numeric validation and decoder-version invalidation.
- Throttled filesystem cache-size scans used by the live Status UI.
- Runtime errors retained for Status even when network logging is disabled.
- Synchronized prefetch/network diagnostics and normalized source extensions.
- **Build status:** pending Windows/MSVC v142 rebuild; Alpha 3 I remains the user-confirmed build/runtime baseline.

# Alpha 3 L — View menu crash fix

**Critical fix:** all commands exposed by View → SACD DLNA now have GUIDs returned by `get_command()`. This prevents the `uBugCheck()` path that could crash foobar2000 when the View menu was opened. See `BUILD.md`.

## v0.8 Alpha 3 J

- Integrated the remaining alpha roadmap items into the current source tree.
- Added server-side next-track prefetch for the next ordered album track, using the existing persistent cache and cache-job coalescing.
- Added explicit network-visibility state based only on traffic initiated by a non-local peer: remote SSDP M-SEARCH and/or remote HTTP client activity.
- Added remote SSDP/HTTP counters and last remote peer reporting to the live Status UI.
- Clarified that local SSDP self-probe validates the local stack but does not prove end-to-end visibility across every network segment.
- Kept richer DIDL-Lite metadata, artwork cache, bounded concurrent clients/cancellation, invalidation-aware cache manifests, Music Library callbacks, network logging, GitHub Actions packaging and T+A renderer negotiation as completed roadmap items.
- Updated the advertised component/server version to `0.8-alpha3-j`.
- **Build status:** Alpha 3 I was confirmed by the maintainer as compiling and running; see `BUILD.md`. Alpha 3 J contains new source changes and requires a fresh Windows/MSVC v142 rebuild.

# Changelog

## v0.8 Alpha 3 H

- Expanded the real-time SACD DLNA Status UI with current music title, artist and album.
- Added source/output format, file size, sample frequency/resolution, channel count and bit depth.
- Added live pipeline/conversion state distinguishing native DSD, SACD ISO decode/cache and DSD Processor output.
- Added effective transmission speed as a real-time multiplier (`x realtime`) alongside TX and required bandwidth.
- Added source and output file-size accounting for the active HTTP stream.
- Kept the previous Alpha 3 G network diagnostics and buffer/read-ahead instrumentation.
- **Build status:** Alpha 3 E remains the last independently build-validated revision; Alpha 3 H requires a fresh Windows/MSVC v142 rebuild.

## v0.8 Alpha 3 G

- Added a real-time `SACD DLNA Status / Diagnostics` UI with 500 ms refresh.
- Added a server read-ahead buffer/reserve bar and explicit buffer health states; documentation clarifies that this is not the renderer's internal playback buffer.
- Added live HTTP/SSDP readiness and discovery counters (`NOTIFY`, `M-SEARCH`, responses, HTTP requests).
- Added HTTP self-test for `device.xml`, `ContentDirectory.xml` and `ConnectionManager.xml`.
- Added an SSDP MediaServer multicast self-probe and external network-presence detection.
- Added `Enable Debug Diagnostics` configuration plus `Run Network Probe`.
- Added `View → SACD DLNA → Run Network Diagnostics` and `Enable Debug Diagnostics`.
- Improved SSDP multicast interface selection and explicit bind/join error reporting.
- Updated the advertised component/server model version to `0.8-alpha3-h`.
- **Build status:** this revision requires a fresh Windows rebuild; Alpha 3 E remains the last confirmed build without errors until Alpha 3 G is rebuilt.

## v0.8 Alpha 3 F

Source changes after the build-validated Alpha 3 E revision:

- Fixed the SACD DLNA status UI popup path: the View → SACD DLNA menu now includes an explicit **Open SACD DLNA Status** command and the popup has an explicit default size/title.
- Added **Configure DSD Processor...** directly to View → SACD DLNA; it opens the installed processor configuration without requiring the Preferences page.
- Improved renderer negotiation by reusing an exact T+A SDX `ConnectionManager::GetProtocolInfo` Sink token when the negotiated MIME matches, instead of regenerating a generic token.
- Made album track ordering deterministic for gapless testing: disc number, track number, title, then item ID.
- Updated the advertised component/server model version to `0.8-alpha3-f`.
- **Build status:** pending Windows rebuild; Alpha 3 E remains the last build-validated revision.

## 0.8-alpha3-d
- Added the official SDK `libPPUI` project explicitly to the solution so the linker dependency is built.
- Fixed the last deprecated SSDP `inet_addr()` call.

## 0.8-alpha3-c
- Fixed `shared-x64.lib` linker path for the official SDK layout.
- Replaced deprecated SSDP `inet_addr()` calls with `InetPtonA()`.
- Added linker-fix documentation.

## 0.8-alpha3-buildfix
- Fixed `cfg_blob` incompatibility with the SDK target level by using `cfg_dsp_chain_config`.
- Fixed abstract `file_info` and `audio_chunk` instantiation by using `file_info_impl` / `audio_chunk_impl_temporary`.
- Fixed WTL UI element integration and Unicode/Win32 drawing calls.
- Fixed `SetDlgItemTextA` / `GetDlgItemTextA` calls to use the dialog window handle.
- Fixed `IDC_HELP` collision by renaming the resource to `IDC_SACD_HELP`.
- Fixed `mainmenu_groups::tools` to use an SDK-defined menu group.
- Fixed `std::filesystem` path conversions under MSVC v142.
- Declared the DSP cache method and corrected DSF writer patch writes.
- Fixed project linker path for `shared-x64.lib`.

## Documentation/toolchain update
- Documented the canonical WTL include path: `D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include`.
- Updated BUILD.md and all project documentation to use the same WTL path.
- Kept the component on MSVC v142.
- Updated build/check scripts to auto-detect the canonical WTL location relative to the SDK tree.
- Added the MIT LICENSE to the project package and clarified third-party licensing.

## 0.8-alpha2-clean-v142-wtl
- Kept `foo_sacd_dlna` on the v142 toolset.
- Fixed WTL property condition so an undefined `WTL_ROOT` does not produce a bogus include path.
- Added `tools/check_build_env.ps1`.
- Added `tools/build.ps1` for reproducible Debug/Release x64 MSBuild invocation with `WTLIncludeDir`.
- Added `BUILD.md` and `BUILD.md`.


## 0.8-alpha1
- Added optional private DLNA DSP processing through installed foo_dsd_processor.
- Added DSD Processor detection/version status and configuration button.
- Added PCM source sharing when DLNA DSD Processor mode is enabled.
- Added DSP preset fingerprint to persistent DSF cache invalidation.
- Added native-DSD output validation after DSP processing.

## 0.7-alpha4
- Added `BUILD.md` with complete Windows compilation/setup guide.
- Added build-machine hardware requirements and runtime requirements.
- Documented Visual Studio 2022, MSVC, Windows SDK and foobar2000 SDK setup.
- Added command-line build, runtime test, DLNA diagnostics, firewall, CI and release checklists.
- Added references to official Microsoft and foobar2000 documentation.

## 0.7-alpha3
- Improved BrowseMetadata and DIDL-Lite metadata/artist roles.
- Renderer-aware protocolInfo selection using ConnectionManager Sink capabilities.
- Added persistent artwork cache with source invalidation.
- Added persistent DSF cache manifest with source/decoder/version invalidation.
- Added cache cleanup UI.
- Added bounded concurrent stream handling and improved request diagnostics.
- Added stream elapsed time and persistent cache size to status.
- Improved Media Library change tracking documentation.
- Added T+A renderer probe and hardware validation plan.
- Added expanded real-DLNA diagnostic documentation.

## 0.7-alpha2
- Real MediaServer browse path with separate BrowseMetadata handling.
- Renderer-aware DSD MIME/protocolInfo negotiation through ConnectionManager.
- Leaf-track BrowseDirectChildren corrected to return zero children.
- Concurrent HTTP client handling and cancellation retained for live renderer requests.
- Persistent/invalidation-aware SACD→DSF cache and duplicate decode coalescing.
- Media Library callback tracking with SystemUpdateID.
- Console + timestamped `network.log` diagnostics.
- Added Windows GitHub Actions build workflow and dependency-free DLNA smoke test.
- Added exact-firmware T+A hardware validation checklist.
- Updated in-app help to explain diagnostics and renderer-dependent gapless behavior.


## 0.6-alpha2
- Added minimum/recommended hardware and network requirements to README.
- Added DSD64/128/256 bandwidth reference.
- Added recommended Gigabit Ethernet topology and Wi-Fi guidance.
- Added NETWORK_REQUIREMENTS.md.

## 0.6 Alpha 1
- Added Stability Mode with persistent SACD→DSD DSF cache.
- Added configurable 5–120 s delivery read-ahead and TCP send-buffer sizing.
- Added conversion progress and buffer/read-ahead status reporting.
- Added required DSD bitrate and network headroom monitoring.


## 0.5-alpha1

- Added live DLNA discovery/broadcasting state.
- Added active audio transmission state.
- Added measured TCP transmit speed in Mbit/s and KiB/s.
- Added total and current-stream byte counters.
- Added active DLNA client IP indication.
- Added DSD rate indication when known.
- Added SSDP/UPnP discovery of T+A renderers.
- Added `T+A SDX: DETECTED / STREAMING` state when the active client matches the detected renderer.
- Added User-Agent based T+A renderer identification when available.
- Added live monitoring to the Preferences page and `SACD DLNA Status` UI element.

## Stability Mode (V0.6)

Stability Mode decouples SACD ISO → DSD conversion from the network delivery path. ISO tracks are converted to a persistent DSF cache and transmission starts only after the DSD file is ready. A configurable 5–120 second read-ahead and a larger TCP send buffer can be used before transmission.

This is useful for short disk/network fluctuations. No server-side buffer can guarantee uninterrupted playback when sustained network throughput is below the bitrate required by the selected DSD rate.

Approximate stereo payload rates: DSD64 = 5.64 Mbit/s; DSD128 = 11.29 Mbit/s; DSD256 = 22.58 Mbit/s.

## Build fix: v142 + WTL
- Changed the component project back to MSVC v142.
- Added configurable WTL include path support.
- Added WTL configuration documentation and build helpers.
- Added explicit build documentation for the `atlapp.h` dependency.

## v0.8 Alpha 3 I
- Fixes the Alpha 3 H compile error in `dlna_server.cpp` by declaring live stream metadata members in `SacdDlnaServer`: channels, bits-per-sample and duration.
- Keeps the Alpha 3 H live audio diagnostics UI and network diagnostics unchanged.
- Build target remains MSVC v142, x64, with WTL from `SDK-2025-03-07\WTL\include`.

### View → SACD DLNA menu hardening — Alpha 3 K

The complete View → SACD DLNA command set was reviewed. Library sharing is now a true toggle, the preferences command opens the dedicated SACD DLNA page directly, and refresh/clear-library/clear-cache actions are available from the same menu. DSD Processor toggling now only re-indexes an already shared/running DLNA library.
## Alpha 3 N — Windows discovery hardening

Improved Windows UPnP/DLNA discovery compatibility: LAN-interface selection for the advertised LOCATION, DLNA device namespace/description, SSDP service announcements and service-type M-SEARCH responses. Added explicit advertised LOCATION diagnostics. Windows Explorer discovery remains dependent on the Windows SSDP/Function Discovery stack and firewall configuration.



## Alpha 3 O — WTL relocation hardening

- Removed the dependency on the literal `WTL` directory name for the component build.
- Added `WTL.props` with marker-header auto-discovery (`include\atlapp.h`) under the SDK root.
- Added `WTL_INCLUDE`, `WTL_ROOT`, and `WTLIncludeDir` override support.
- Added `WTL.user.props.example` for machine-local overrides.
- Updated `tools\build.ps1` and `tools\check_build_env.ps1` to use the same WTL discovery logic.
- Updated the WTL documentation to describe renamed-folder layouts.
- Alpha 3 O requires a fresh Windows/MSVC v142 build validation.


### Alpha 3 P — WTL discovery fix

Fixed invalid MSBuild condition in relocatable WTL discovery introduced by Alpha 3 O.


### Alpha 3 Q — WTL relocation / MSBuild fix

Alpha 3 Q removes the invalid MSBuild item-list-to-property conversion from WTL discovery. It also adds an SDK-root `Directory.Build.targets` overlay so WTL headers are injected into referenced projects such as libPPUI and foobar2000_sdk_helpers. The `tools/install_wtl_support.ps1` script auto-detects any WTL folder containing `include\atlapp.h`, independent of the folder name.

## 0.8 Alpha 3 R — WTL/MSBuild relocation fix

- Removed all `@(WTLMarker...)` expressions from compiler item-definition metadata.
- WTL auto-discovery now runs in a target immediately before compilation.
- Discovered WTL include path is injected through `ClCompile Update`, which is valid MSBuild metadata handling.
- Improved diagnostics for missing `atlapp.h`.

## 0.8 Alpha 3 S — compile fixes
- Fixed `const std::mutex` locking in live status by making the prefetch mutex mutable.
- Fixed ambiguous signed/unsigned `jsonNumberFieldEquals()` overload resolution with explicit integer casts.
- Added `tools/apply_sdk_wtl_patch.ps1` and `BUILD.md` so WTL include discovery is propagated to libPPUI and foobar2000_sdk_helpers.

## Current fix
- Keep `/media/<id>` stable across library refreshes using source path + subsong identity.
- Prevent stale UPnP Browse URLs from turning into HTTP 404 while the track remains shared.
