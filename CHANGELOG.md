# Alpha 3 L — View menu crash fix

**Critical fix:** all commands exposed by View → SACD DLNA now have GUIDs returned by `get_command()`. This prevents the `uBugCheck()` path that could crash foobar2000 when the View menu was opened. See `BUILD_VALIDATION_0.8_ALPHA3_L.md`.

## v0.8 Alpha 3 J

- Integrated the remaining alpha roadmap items into the current source tree.
- Added server-side next-track prefetch for the next ordered album track, using the existing persistent cache and cache-job coalescing.
- Added explicit network-visibility state based only on traffic initiated by a non-local peer: remote SSDP M-SEARCH and/or remote HTTP client activity.
- Added remote SSDP/HTTP counters and last remote peer reporting to the live Status UI.
- Clarified that local SSDP self-probe validates the local stack but does not prove end-to-end visibility across every network segment.
- Kept richer DIDL-Lite metadata, artwork cache, bounded concurrent clients/cancellation, invalidation-aware cache manifests, Music Library callbacks, network logging, GitHub Actions packaging and T+A renderer negotiation as completed roadmap items.
- Updated the advertised component/server version to `0.8-alpha3-j`.
- **Build status:** Alpha 3 I was confirmed by the maintainer as compiling and running; see `BUILD_VALIDATION_0.8_ALPHA3_I.md`. Alpha 3 J contains new source changes and requires a fresh Windows/MSVC v142 rebuild.

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
- Added `WTL_SETUP.md` and `V142_WTL_FIX.md`.


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
- Added WTL_SETUP.md and a PowerShell helper for configuring SDK projects.
- Added explicit build documentation for the `atlapp.h` dependency.

## v0.8 Alpha 3 I
- Fixes the Alpha 3 H compile error in `dlna_server.cpp` by declaring live stream metadata members in `SacdDlnaServer`: channels, bits-per-sample and duration.
- Keeps the Alpha 3 H live audio diagnostics UI and network diagnostics unchanged.
- Build target remains MSVC v142, x64, with WTL from `SDK-2025-03-07\WTL\include`.

### View → SACD DLNA menu hardening — Alpha 3 K

The complete View → SACD DLNA command set was reviewed. Library sharing is now a true toggle, the preferences command opens the dedicated SACD DLNA page directly, and refresh/clear-library/clear-cache actions are available from the same menu. DSD Processor toggling now only re-indexes an already shared/running DLNA library.
