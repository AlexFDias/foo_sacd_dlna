# foo_sacd_dlna — Roadmap

Per-revision fix notes (Alpha 3 K/L/M/N, the DVD-Audio FLAC v6–v10 line, etc.) live in `CHANGELOG.md`; this file tracks what's done at a feature level and what external validation is still outstanding. See `docs/VALIDATION_STATUS.md` for the precise, honest distinction between "exists in source" and "confirmed working."

## Completed in source

| Area | Status |
| --- | --- |
| UPnP ContentDirectory Browse / BrowseMetadata pagination | done |
| Artist → Album → Track Music Library tree | done |
| Renderer-aware DSD protocolInfo negotiation | done |
| Deterministic album track ordering | done |
| Server-side next-track cache prefetch | done |
| DIDL-Lite metadata: artist, album artist, genre, track/disc, date, composer, publisher, comment | done |
| Duration / DSD resolution / channel / bitrate metadata | done |
| Album-art serving and persistent artwork cache | done |
| Concurrent HTTP clients with abort/cancellation | done |
| Persistent SACD→DSF cache with source/version invalidation | done |
| Persistent DSP→DSF cache with DSP version/preset invalidation | done |
| DVD-Audio → FLAC via the real libFLAC 1.5.x encoder, with metadata-chain cache validation | done |
| Music Library callbacks and SystemUpdateID | done |
| SSDP MediaServer announcements and M-SEARCH responses | done |
| UPnP device/service XML and SOAP control endpoints | done |
| Live buffer/read-ahead UI | done |
| Live music/source/output/DSP pipeline diagnostics | done |
| Network self-tests and explicit remote-visibility evidence | done |
| Console + rotating `network.log` diagnostics, including a startup warning when `libFLAC.dll` is missing | done |
| Windows GitHub Actions build/package workflow | done (needs `FOOBAR2000_SDK_URL`/`WTL_URL` repo variables set — see `BUILD.md` section 27) |
| T+A SDX discovery and ConnectionManager Sink negotiation | done |
| Logical-stream-aware Range/seek handling (a renderer's own seek doesn't cost it a second "Max streams" slot) | done |
| `SO_SNDTIMEO` on the media socket, so a dead connection releases its stream slot promptly instead of holding it indefinitely | done |
| T+A SDX exact-firmware validation checklist | done (checklist only — see `HARDWARE_VALIDATION.md`) |

## External validation still required

- A fresh Windows/MSVC v142 build of the current tree in an environment that actually has the toolchain (none is available where this source was last edited).
- `libFLAC.dll` deployed next to the installed `foo_sacd_dlna.dll`, confirmed by the absence of the startup Console warning.
- The `--ssdp` smoke test run from a second LAN device.
- Confirming the SDX 3100 HV (or any target renderer) appears as a remote SSDP peer and/or HTTP client in the live Status UI.
- DVD-Audio → FLAC playback validated end-to-end, including `flac.exe -t` / `metaflac.exe --list` against the generated cache.
- Gapless transitions validated on the exact renderer firmware in use.
- DSD64/128/256 playback, Range requests and real sustained network throughput validated on the actual target network.

See `RELEASE_CHECKLIST.md` for the full pre-release checklist and `docs/VALIDATION_STATUS.md` for real diagnostic evidence gathered so far (which already narrows most current DVD-Audio playback failures to the missing-`libFLAC.dll` deployment step above, rather than a source defect).

## Network visibility semantics

`SSDP NOTIFY sent` proves that the server sent multicast announcements. `SSDP self-probe` proves local MediaServer discovery through the local network stack. `CONFIRMED / REMOTE SSDP M-SEARCH` or `CONFIRMED / REMOTE HTTP` proves that a non-local device actually reached the server.
