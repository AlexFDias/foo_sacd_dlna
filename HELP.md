# foo_sacd_dlna — Quick Help / FAQ

This is a short, task-oriented help page. For full detail see `docs/USER_GUIDE.md` (usage), `BUILD.md` (compiling), and `DOCUMENTATION_INDEX.md` (everything else).

## What this component does

`foo_sacd_dlna` is a UPnP/DLNA MediaServer for foobar2000. It shares your Music Library over the network so a DLNA renderer (a network audio streamer, a TV, a control-point app, etc.) can browse and play it:

- **DSF/DFF** files are served as-is.
- **SACD ISO** is decoded through `foo_input_sacd` and served as native DSD/DSF (the `.iso` container itself is never sent).
- **DVD-Audio** is decoded through `foo_input_dvda` and served as cached lossless 24-bit **FLAC**, via the real Xiph libFLAC 1.5.x encoder.
- Any other shared format (FLAC/WAV/MP3/...) can optionally be served as-is too, via **Shared formats** in Settings.

## Fastest way to get playing

1. `File → Preferences → Tools → SACD DLNA → Settings`: enable DLNA, enable Share Music Library.
2. Point your renderer/control-point app at this PC; it should discover the server automatically over SSDP.
3. Watch `Status`: `BROADCASTING / ACTIVE` means the server is up; `TRANSMITTING` means audio is actually flowing to a client right now.

## "It plays DSF but not DVD-Audio tracks" — read this first

By far the most common cause is **not a bug**: DVD-Audio → FLAC needs `libFLAC.dll` (Win64, 1.5.x) sitting in the *same folder as the installed* `foo_sacd_dlna.dll` — not just in the source tree's build-output folder. If it's missing, every DVD-Audio track fails, one at a time, as an HTTP `503` on the renderer (which many renderers/players then report as a generic "invalid or unknown format" error).

From this build onward, the foobar2000 **Console** tells you immediately at startup if this is the problem:

```text
SACD DLNA: libFLAC.dll was not found at "...\libFLAC.dll" -- DVD-Audio to FLAC
conversion will fail for every track until it is copied there (see FLAC_RUNTIME.md).
```

Fix: copy `third_party/libFLAC/Win64/libFLAC.dll` (from the source tree, or from a matching FLAC 1.5.0 Win64 release) into the folder where `foo_sacd_dlna.dll` is actually installed, then restart foobar2000. See `FLAC_RUNTIME.md` and `BUILD.md` (section 15) for the full explanation.

## Other common issues

| Symptom | Likely cause | Where to look |
|---|---|---|
| Renderer never sees the server at all | SSDP multicast blocked, or PC/renderer on different subnets/VLANs | `NETWORK_REQUIREMENTS.md`, `PROTOCOL_COMPATIBILITY.md` |
| Works for a bit, then other tracks start returning 503 | Concurrent-stream limit reached, often from abandoned connections on track-skip | `PREFERENCES_FIELDS.md` ("Max streams"), `CHANGELOG.md` (`SO_SNDTIMEO` fix) |
| SACD ISO tracks fail to prepare | `foo_input_sacd` missing, or it didn't return a native DSD/DoP stream for that particular ISO | Preferences → Status: `foo_input_sacd` detection line |
| Playback stutters on Wi-Fi | Bitrate/latency; DSD256 in particular needs a solid link | `NETWORK_REQUIREMENTS.md` |
| Unsure whether audio is actually being sent | `BROADCASTING` only means discovery is up, not that a stream is flowing | `Status` panel / `/status` web page |

## Where to go next

- Installing a build you already have (or just downloaded): `INSTALL.md`
- Everyday usage and settings: `docs/USER_GUIDE.md`, `PREFERENCES_FIELDS.md`
- Building from source: `BUILD.md`
- Architecture / how it works internally: `docs/ARCHITECTURE.md`
- What has and hasn't actually been validated: `docs/VALIDATION_STATUS.md`
- Full documentation map: `DOCUMENTATION_INDEX.md`
