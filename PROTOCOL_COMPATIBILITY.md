# UPnP/DLNA Protocol Compatibility

## SDX 3100 HV

T+A's current SDX 3100 HV specification lists the following Streaming Client formats:

- DFF
- DSF
- DSD64
- DSD128
- DSD256

The same specification lists 10/100/1000 Base-T Ethernet and IEEE 802.11a/b/g/n/ac/ax Wi-Fi.

The SDX's internal converter supports higher native DSD rates, including DSD512 and DSD1024, but that is not evidence that those rates are accepted by the network Streaming Client. This project therefore treats the network path separately and targets DSD64/128/256.

## MIME / protocolInfo strategy

The server starts with standard DSF/DFF MIME candidates and, when a T+A renderer is discovered, reads its UPnP `ConnectionManager::GetProtocolInfo` `Sink` string.

For DSF the server prefers the MIME value actually advertised by the renderer (for example `audio/x-dsf` or `audio/dsf`). DFF is handled similarly.

This is intentionally **capability negotiation**, not a firmware-specific hardcoded workaround.

## Exact firmware validation

A firmware-specific compatibility claim requires an actual SDX 3100 HV unit with its exact firmware version. Run:

```text
python tools/ta_sdx_probe.py <SDX-IP>
```

and complete the matrix in `HARDWARE_VALIDATION.md`.

Do not add a hardcoded T+A workaround based only on a third-party report or on a different firmware revision.

## Gapless

The MediaServer provides exact duration metadata, ordered track browsing and HTTP byte-range support. Whether the SDX performs seamless track transitions is ultimately renderer/firmware behaviour and must be measured on the target unit.

## Status interpretation

`BROADCASTING` indicates that the MediaServer/SSDP service is active. It does not mean an audio stream is being sent.

`TRANSMITTING` indicates that an HTTP media request is actively being served. The live TX counter is based on bytes written to the client socket.

`T+A SDX: DETECTED / STREAMING` is a correlation of the discovered renderer identity with the active HTTP client IP. It is a network-level indication, not a readout of the physical SDX display.

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
```

See `BUILD.md`, `WTL_SETUP.md` and `V142_WTL_FIX.md` for the complete configuration.
