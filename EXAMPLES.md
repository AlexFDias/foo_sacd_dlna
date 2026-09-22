# foo_sacd_dlna Examples

## 1. First-time setup

1. Install foobar2000 x64.
2. Install the Super Audio CD Decoder (`foo_input_sacd`).
3. Install `foo_sacd_dlna`.
4. Open **Preferences → Tools → SACD DLNA**.
5. Enable **DLNA broadcasting**.
6. Enable **Share DSD content from foobar2000 Music Library**.
7. Browse the server from the T+A SDX 3100 HV.

Expected idle status:

```text
DLNA: BROADCASTING / ACTIVE
Audio stream: IDLE
T+A SDX: DETECTED / IDLE
```

## 2. DSF file

For an existing `.dsf` file, no SACD ISO conversion is needed. The server exposes the track directly and the T+A requests the DSF through HTTP.

## 3. SACD ISO

For:

```text
D:\\Music\\SACD\\Album.iso
```

The network path is:

```text
SACD ISO
  ↓
foo_input_sacd
  ↓
DSD
  ↓
persistent DSF cache
  ↓
HTTP / DLNA
  ↓
T+A SDX 3100 HV
```

The source ISO is never modified.

## 4. Check that the SDX is really receiving audio

Do not use only `BROADCASTING / ACTIVE`. Look for:

```text
Audio stream: ACTIVE / TRANSMITTING
T+A SDX: DETECTED / STREAMING
TX speed: 22.x Mbit/s
DSD: DSD256
```

`BROADCASTING` is the discovery state. `TRANSMITTING` means actual HTTP media data is being sent.

## 5. Busy network

Recommended:

```text
Stability Mode: ON
Pre-buffer: 20-30 seconds
Ethernet: 1 Gbps
```

The buffer absorbs short interruptions. It cannot compensate for a connection whose sustained throughput is below the DSD payload requirement.

## 6. Debugging the protocol without the SDX

Run:

```text
python tools/dlna_smoke_test.py <PC-IP> 8192
python tools/dlna_protocol_report.py <PC-IP> 8192
```

The smoke test checks device description, ContentDirectory Browse/BrowseMetadata, ConnectionManager GetProtocolInfo, media HEAD and HTTP Range.

To inspect the real T+A renderer:

```text
python tools/ta_sdx_probe.py <SDX-IP>
```

## 7. Gapless test

Use an album with two consecutive tracks. Start playback and observe:

- whether the first track has correct duration;
- whether the renderer requests the second track before the first ends;
- whether there is an audible pause;
- whether a Range request is used.

Record the result with the exact SDX firmware. Do not treat gapless as guaranteed until this test passes on the target firmware.

## 8. Sharing non-DSD formats (FLAC, WAV, MP3, ...)

DSD content (`.dsf`, `.dff`, SACD `.iso`) is shared by default. To also
share other formats already in your Media Library:

1. Preferences → SACD DLNA → **Shared formats**, change `dsf,dff,iso` to
   e.g. `dsf,dff,iso,flac,wav,mp3`.
2. Apply, then **Refresh library** (or toggle **Share DSD content** off and
   on) so the new filter takes effect.

With **DSD Processor** off (the default), the newly-added formats are sent
to the renderer exactly as stored on disk - no conversion, no re-encoding,
just a normal DLNA file serve with `HTTP Range` support. SACD ISO is the one
exception: it is always decoded and advertised as `.dsf`/DSD, since the raw
`.iso` container isn't something a renderer can play directly.

With **DSD Processor** on, non-DSD formats can instead be converted to DSD
before serving (same pipeline as SACD ISO uses) - see `DSP_PROCESSOR.md`.

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
<SDK root>\<WTL folder>\include
```

See `BUILD.md`, `WTL_SETUP.md` and `V142_WTL_FIX.md` for the complete configuration.
