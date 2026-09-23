# Alpha 3 M — code hardening

### Alpha 3 M — completed hardening
- [x] Lifecycle-safe start/stop recovery
- [x] HTTP concurrency-limit response ordering
- [x] Exact cache-manifest validation and decoder-version invalidation
- [x] Live diagnostic synchronization and filesystem-scan throttling
- [x] Prefetch worker/state cleanup hardening
- [ ] Fresh Windows/MSVC v142 rebuild

# Alpha 3 L — View menu crash fix

**Critical fix:** all commands exposed by View → SACD DLNA now have GUIDs returned by `get_command()`. This prevents the `uBugCheck()` path that could crash foobar2000 when the View menu was opened. See `BUILD.md`.

# foo_sacd_dlna Roadmap — v0.8 Alpha 3 M

## Completed in source

| Area | Status |
| --- | --- |
| UPnP ContentDirectory Browse / BrowseMetadata pagination | ✅ |
| Artist → Album → Track Music Library tree | ✅ |
| Renderer-aware DSD protocolInfo negotiation | ✅ |
| Deterministic album track ordering | ✅ |
| Server-side next-track cache prefetch | ✅ |
| DIDL-Lite metadata: artist, album artist, genre, track/disc, date, composer, publisher, comment | ✅ |
| Duration / DSD resolution / channel / bitrate metadata | ✅ |
| Album-art serving and persistent artwork cache | ✅ |
| Concurrent HTTP clients with abort/cancellation | ✅ |
| Persistent SACD→DSF cache with source/version invalidation | ✅ |
| Persistent DSP→DSF cache with DSP version/preset invalidation | ✅ |
| Music Library callbacks and SystemUpdateID | ✅ |
| SSDP MediaServer announcements and M-SEARCH responses | ✅ |
| UPnP device/service XML and SOAP control endpoints | ✅ |
| Live buffer/read-ahead UI | ✅ |
| Live music/source/output/DSP pipeline diagnostics | ✅ |
| Network self-tests and explicit remote-visibility evidence | ✅ |
| Console + rotating `network.log` diagnostics | ✅ |
| Windows GitHub Actions build/package workflow | ✅ |
| T+A SDX discovery and ConnectionManager Sink negotiation | ✅ |
| T+A SDX exact-firmware validation checklist | ✅ (checklist) |

## External validation still required

- Rebuild the current Alpha 3 M revision on the maintainer's Windows/MSVC v142 environment.
- Run the `--ssdp` smoke test from a second LAN device.
- Confirm the SDX 3100 HV appears as a remote SSDP peer and/or HTTP client in the live Status UI.
- Validate gapless transitions on the exact SDX 3100 HV firmware in use.
- Validate DSD64/128/256 playback, Range requests and real sustained network throughput on the actual network.

## Network visibility semantics

`SSDP NOTIFY sent` proves that the server sent multicast announcements. `SSDP self-probe` proves local MediaServer discovery through the local network stack. `CONFIRMED / REMOTE SSDP M-SEARCH` or `CONFIRMED / REMOTE HTTP` proves that a non-local device actually reached the server.

### View → SACD DLNA menu hardening — Alpha 3 K

The complete View → SACD DLNA command set was reviewed. Library sharing is now a true toggle, the preferences command opens the dedicated SACD DLNA page directly, and refresh/clear-library/clear-cache actions are available from the same menu. DSD Processor toggling now only re-indexes an already shared/running DLNA library.
## Alpha 3 N — Windows discovery hardening

Improved Windows UPnP/DLNA discovery compatibility: LAN-interface selection for the advertised LOCATION, DLNA device namespace/description, SSDP service announcements and service-type M-SEARCH responses. Added explicit advertised LOCATION diagnostics. Windows Explorer discovery remains dependent on the Windows SSDP/Function Discovery stack and firewall configuration.

