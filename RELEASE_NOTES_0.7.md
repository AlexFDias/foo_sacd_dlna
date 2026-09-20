# foo_sacd_dlna 0.7 Alpha 3

This release moves the project further toward real-world UPnP/DLNA interoperability testing with the T+A SDX 3100 HV.

## Included

- More complete `BrowseMetadata` / DIDL-Lite information.
- Renderer-aware `protocolInfo` selection based on `ConnectionManager::GetProtocolInfo`.
- Persistent album-art cache with source-stat invalidation.
- Persistent DSF cache manifests with cache-format, source and decoder-version invalidation.
- Explicit cache cleanup from Preferences.
- Bounded concurrent media transfers.
- Better cancellation and request diagnostics.
- Live stream elapsed time, TX rate, client identity and cache size.
- T+A renderer probe tool.
- Hardware-validation test matrix for exact SDX firmware.
- Expanded Help/README documentation.

## Gapless

The server-side prerequisites are improved (exact duration, ordered tracks, range support), but gapless transition is renderer/firmware dependent and remains a validation item.

## Validation status

The component has **not** been physically validated against an SDX 3100 HV firmware supplied by the user. Use `HARDWARE_VALIDATION.md` before claiming firmware-specific compatibility.


See `RELEASE_CHECKLIST.md` before publishing a release.

See `PROTOCOL_COMPATIBILITY.md` and `RELEASE_CHECKLIST.md`.

## Notes for testers

This alpha is source-complete for the currently implemented MediaServer path, but it is **not a firmware certification**. The exact T+A SDX 3100 HV firmware in use still has to be recorded and tested using `HARDWARE_VALIDATION.md`.

A successful build is not sufficient to claim gapless or renderer-specific compatibility. Those require a physical playback test.
