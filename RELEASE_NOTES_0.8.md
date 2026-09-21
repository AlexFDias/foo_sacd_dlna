# Alpha 3 M — code hardening

Alpha 3 M performs a source-level robustness audit over lifecycle, HTTP/DLNA, caching, diagnostics and concurrency. It is pending a fresh Windows/MSVC v142 build.

# Release Notes — v0.8 Alpha 3 L — View menu crash fix

**Critical fix:** all commands exposed by View → SACD DLNA now have GUIDs returned by `get_command()`. This prevents the `uBugCheck()` path that could crash foobar2000 when the View menu was opened. See `BUILD_VALIDATION_0.8_ALPHA3_L.md`.

# Release Notes — v0.8 Alpha 3 J

Alpha 3 J consolidates the implemented alpha roadmap around live diagnostics, renderer interoperability, cache reliability and network visibility.

## Roadmap integration

- Server-side next-track prefetch follows the deterministic album track order and warms the existing SACD/DSP DSF cache before the renderer requests the next item.
- DIDL-Lite metadata, duration, DSD resolution and cached artwork remain exposed through ContentDirectory.
- HTTP client concurrency is bounded and abort-aware.
- SACD→DSF and DSP→DSF caches remain persistent and invalidation-aware through source/version/preset manifests.
- Media Library callbacks update `SystemUpdateID` and refresh shared content.
- Console and `network.log` diagnostics are retained.
- Windows GitHub Actions packaging workflow and T+A SDX hardware checklist are included.

## Network visibility

The live Status UI now reports whether visibility has been proven by a non-local peer: `CONFIRMED / REMOTE SSDP M-SEARCH` or `CONFIRMED / REMOTE HTTP`. Local self-probe remains separate.

## Validation

Alpha 3 I was confirmed by the maintainer as compiling successfully and running. Alpha 3 J is a subsequent source revision and needs a fresh Windows/MSVC v142 rebuild before its build status is marked validated.

### View → SACD DLNA menu hardening — Alpha 3 K

The complete View → SACD DLNA command set was reviewed. Library sharing is now a true toggle, the preferences command opens the dedicated SACD DLNA page directly, and refresh/clear-library/clear-cache actions are available from the same menu. DSD Processor toggling now only re-indexes an already shared/running DLNA library.
## Alpha 3 N — Windows discovery hardening

Improved Windows UPnP/DLNA discovery compatibility: LAN-interface selection for the advertised LOCATION, DLNA device namespace/description, SSDP service announcements and service-type M-SEARCH responses. Added explicit advertised LOCATION diagnostics. Windows Explorer discovery remains dependent on the Windows SSDP/Function Discovery stack and firewall configuration.

