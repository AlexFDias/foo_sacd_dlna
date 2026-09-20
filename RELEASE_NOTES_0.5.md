# foo_sacd_dlna 0.5-alpha1

This alpha adds network activity monitoring for the T+A SDX 3100 HV use case.

### Status meanings

`DLNA discovery: BROADCASTING / ACTIVE` — the MediaServer is announcing/discoverable through SSDP.

`Audio stream: ACTIVE / TRANSMITTING` — a renderer has an HTTP media connection and bytes are actively being sent.

`TX` — measured TCP data rate from foobar2000 to the renderer.

`T+A SDX: DETECTED / STREAMING` — the renderer was identified as T+A/SDX and its IP is the current media client.

`T+A SDX: DETECTED / IDLE` — the renderer was discovered but is not currently requesting audio.

This is intentionally not described as an audio broadcast: SSDP handles discovery; the audio payload is delivered by HTTP to the renderer.

### Compatibility target

T+A currently documents the SDX 3100 HV streaming client as supporting UPnP/DLNA and DFF/DSF with DSD64/DSD128/DSD256. Exact interoperability should be tested with the firmware installed on the target SDX.

## Stability Mode (V0.6)

Stability Mode decouples SACD ISO → DSD conversion from the network delivery path. ISO tracks are converted to a persistent DSF cache and transmission starts only after the DSD file is ready. A configurable 5–60 second read-ahead and a larger TCP send buffer can be used before transmission.

This is useful for short disk/network fluctuations. No server-side buffer can guarantee uninterrupted playback when sustained network throughput is below the bitrate required by the selected DSD rate.

Approximate stereo payload rates: DSD64 = 5.64 Mbit/s; DSD128 = 11.29 Mbit/s; DSD256 = 22.58 Mbit/s.
