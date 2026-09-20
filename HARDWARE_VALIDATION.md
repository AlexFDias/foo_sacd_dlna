# T+A SDX 3100 HV Hardware Validation Plan

## What is verified automatically

`foo_sacd_dlna` can discover a UPnP MediaRenderer, inspect its device description, identify a T+A/SDX renderer where the device exposes those strings, and request `ConnectionManager::GetProtocolInfo`.

The status page then shows:

- renderer IP;
- friendly name;
- model/model number where available;
- negotiated Sink protocol information;
- active streaming correlation based on the HTTP client IP.

## What requires the exact unit/firmware

The project intentionally does not claim that every firmware revision has identical DLNA behaviour.

Record on the test unit:

```text
Model: SDX 3100 HV
Firmware version: __________________
MusicNavigator version (if relevant): __________________
LAN/Wi-Fi: __________________
IP address: __________________

```

Run:

```text
python tools/ta_sdx_probe.py <SDX-IP>
```

Then run the MediaServer smoke test against the PC running foobar2000:

```text
python tools/dlna_smoke_test.py <PC-IP> 8192
```

## Playback matrix

Test at minimum:

| Test | Result |
|---|---|
| Discovery | PASS / FAIL |
| Device description | PASS / FAIL |
| Browse root | PASS / FAIL |
| Browse Artist | PASS / FAIL |
| Browse Album | PASS / FAIL |
| BrowseMetadata | PASS / FAIL |
| DSF DSD64 | PASS / FAIL |
| DSF DSD128 | PASS / FAIL |
| DSF DSD256 | PASS / FAIL |
| SACD ISO → DSF | PASS / FAIL |
| Album art | PASS / FAIL |
| Range/seek | PASS / FAIL |
| Next track | PASS / FAIL |
| Gapless transition | PASS / FAIL |
| 30+ min DSD256 | PASS / FAIL |
| Network congestion recovery | PASS / FAIL |

## Evidence to collect

For each failing case enable network logging and capture:

- foobar2000 Console output;
- `foo_sacd_dlna\\network.log`;
- `/status` page output;
- requested URL;
- HTTP status and Range headers;
- negotiated Sink protocol string;
- DSD rate and measured TX bitrate;
- exact SDX firmware version.

Only after these tests should renderer-specific hardcoded workarounds be added.

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
```

See `BUILD.md`, `WTL_SETUP.md` and `V142_WTL_FIX.md` for the complete configuration.
