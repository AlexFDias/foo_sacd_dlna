# Preferences Field Reference

| Field | Purpose | Typical value |
|---|---|---|
| Enable DLNA broadcasting | Starts SSDP discovery and HTTP MediaServer | ON |
| Share DSD content | Publishes DSD items from the foobar2000 Media Library | ON |
| Server name | Name displayed by UPnP players | foobar2000 SACD DSD |
| HTTP port | TCP port for HTTP/XML/media delivery | 8192 |
| Enable stability mode | Separates cache preparation from network delivery | ON |
| Pre-buffer (seconds) | Target DSD read-ahead | 15 |
| Verbose network logging | Adds timestamps/protocol diagnostics to the log | OFF normally |
| Enable debug diagnostics | Enables the richer live diagnostics panel and diagnostic logging | OFF normally |
| Run Network Probe | Validates local HTTP UPnP endpoints and performs an SSDP MediaServer multicast self-probe | Manual |

## Live status fields

| Status | Meaning |
|---|---|
| DLNA | `BROADCASTING / ACTIVE` means the MediaServer/SSDP service is running. |
| foo_input_sacd | Required decoder detection and version. |
| Music Library | Whether the DSD library is shared and item count. |
| Audio stream | `TRANSMITTING` means actual HTTP media delivery. |
| TX speed | Measured TCP transmit rate. |
| T+A SDX | Detected renderer identity and active-stream correlation. |
| DSD read-ahead | Server-side DSD read-ahead reserve and live state/percentage. It is not the T+A playback buffer. |
| Network | HTTP self-test, SSDP self-probe, NOTIFY/M-SEARCH counters and external network presence. |

| Process DLNA audio through DSD Processor | Runs the installed DSD Processor as a private DLNA-only DSP chain. Result must be DSD. | OFF (native DSD) |
| Configure DSD Processor... | Opens the installed DSP Processor configuration and saves its preset for DLNA use. | Configure per desired DSD output |

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
```

See `BUILD.md`, `WTL_SETUP.md` and `V142_WTL_FIX.md` for the complete configuration.

## Alpha 3 H — live audio information

The SACD DLNA Status panel now shows the active music title/artist/album, source and output formats, source/output file size, DSD/PCM sample rate, channels and bit depth, effective network speed in x-realtime, and the active processing pipeline. The conversion label distinguishes native DSD (`NO CONVERSION`), SACD ISO decoding/cache, and DSD Processor output (`DSP OUTPUT / CACHED` or `DSP CONVERTING`).

## Alpha 3 H — live audio diagnostics

The diagnostic UI includes current music metadata, source/output format and size, audio resolution, measured TX speed, x-realtime transmission rate, and explicit native/DSP/SACD conversion states.
