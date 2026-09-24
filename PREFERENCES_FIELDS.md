# Preferences Field Reference

| Field | Purpose | Typical value |
|---|---|---|
| Enable DLNA broadcasting | Starts SSDP discovery and HTTP MediaServer | ON |
| Share DSD content | Publishes DSD items from the foobar2000 Media Library | ON |
| Shared formats | Comma/space-separated extension filter for what gets shared (default `dsf,dff,iso`). SACD ISO is always advertised to renderers as native DSD/DSF - the ISO container itself is never sent. Add other extensions (e.g. `flac,wav,mp3`) to share them too: with DSD Processor disabled they're sent in their original format (no conversion); with it enabled, non-DSD formats can be converted to DSD. | `dsf,dff,iso` |
| Server name | Name displayed by UPnP players | foobar2000 SACD DSD |
| HTTP port | TCP port for HTTP/XML/media delivery | 8192 |
| Max streams | Maximum number of audio streams served at the same time (1-16). A request over the limit is answered `503 Service Unavailable` and the renderer retries. Applies to the next request; streams already running are never cut. Each extra stream costs disk, CPU (SACD/DSP conversion) and network. | 2 |
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
| Clients | `total` = `active` + `idle`. A client is a remote device that sent HTTP requests to this server (this machine's own self-test traffic is not counted). `active` = being sent audio right now (a request that is only waiting for a SACD/DSP conversion is not a stream yet); `idle` = known but not streaming (forgotten after 10 minutes of silence). Also shown: streams accepted / allowed (the Max streams limit) and how many requests were rejected. The `/status` web page adds a per-client table. |
| TX speed | Measured TCP transmit rate. |
| T+A SDX | Detected renderer identity and active-stream correlation. |
| DSD read-ahead | Server-side DSD read-ahead reserve and live state/percentage. It is not the T+A playback buffer. |
| Network | HTTP self-test, SSDP self-probe, NOTIFY/M-SEARCH counters and external network presence. |

| Process DLNA audio through DSD Processor | Runs the installed DSD Processor as a private DLNA-only DSP chain. Result must be DSD. | OFF (native DSD) |
| Configure DSD Processor... | Opens the installed DSP Processor configuration and saves its preset for DLNA use. | Configure per desired DSD output |


## Alpha 3 H — live audio information

The SACD DLNA Status panel now shows the active music title/artist/album, source and output formats, source/output file size, DSD/PCM sample rate, channels and bit depth, effective network speed in x-realtime, and the active processing pipeline. The conversion label distinguishes native DSD (`NO CONVERSION`), SACD ISO decoding/cache, and DSD Processor output (`DSP OUTPUT / CACHED` or `DSP CONVERTING`).

## Alpha 3 H — live audio diagnostics

The diagnostic UI includes current music metadata, source/output format and size, audio resolution, measured TX speed, x-realtime transmission rate, and explicit native/DSP/SACD conversion states.
