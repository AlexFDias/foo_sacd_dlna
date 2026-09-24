# DVD-Audio FLAC — DLNA compatibility fix

## Root cause addressed

The generated FLAC was already being accepted by VLC as FLAC PCM audio. The DLNA description, however, was inconsistent: DIDL-Lite and the HTTP resource exposed FLAC, while `ConnectionManager::GetProtocolInfo` did not advertise FLAC in its `Source` list. A strict renderer can therefore reject the resource before attempting playback.

## Changes

- `GetProtocolInfo` now advertises `audio/flac` and `audio/x-flac` with the FLAC DLNA profile.
- FLAC HTTP responses now publish `DLNA.ORG_PN=FLAC` in `contentFeatures.dlna.org`.
- DIDL-Lite continues to use `audio/flac` and the same FLAC profile.
- Existing `HEAD`, byte-range, `206 Partial Content`, `Content-Range` and read-ahead support is retained.
- libFLAC 1.5.0 remains the encoder; no return to the handwritten FLAC encoder.

## Validation status

The repository contains the source correction. A fresh Visual Studio build is still required in this environment (no Windows toolchain here).

A real playback test on the target T+A renderer has since been run and diagnosed via `network.log` and a VLC diagnostic dump (see `docs/VALIDATION_STATUS.md`): the DLNA/FLAC description fix, the metadata-chain FLAC validator and the `SO_SNDTIMEO` streaming-timeout logic did not show up as a failure cause. Every DVD-Audio conversion failure in that session was `libFLAC.dll 1.5.x was not found next to foo_sacd_dlna` — a missing runtime file in the deployed component folder, not a defect in the fixes described here. See `FLAC_RUNTIME.md` for the startup Console warning that now surfaces this immediately.

## Media URL stability fix

DVD-Audio FLAC media URLs are now resolved by the stable server item ID and no longer require `metadb_handle::is_valid()` to be true. This is important because the cached FLAC is generated from `sourcePath` + `subsong`; a library/metadb handle can become invalid while the cached media remains valid.

The `/media/<id>.flac` handler also falls back to a direct item-ID scan if the O(1) index is stale, and logs an explicit `MEDIA 404 ... reason=ITEM_NOT_FOUND` diagnostic when the ID is genuinely unknown.


### v5: DVD-Audio decoder seeking

The HTTP 503 result in the latest VLC diagnostic proves the media ID and HTTP route are reached, but does not identify the conversion error. The DVD-Audio decoder is now initialized without `input_flag_no_seeking`, because DVD navigation/program decoding may require internal seeks even though the resulting HTTP stream is sequential. The 503 response also exposes the component's stored conversion error to make the next test conclusive.
