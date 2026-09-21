# Alpha 3 O — relocatable WTL build

The project no longer depends on the WTL directory being named `WTL`. `WTL.props` auto-discovers a sibling directory containing `include\atlapp.h`; explicit `WTLIncludeDir`, `WTL_INCLUDE`, and `WTL_ROOT` overrides remain supported. See `WTL_RELOCATION.md`.


**Critical fix:** all commands exposed by View → SACD DLNA now have GUIDs returned by `get_command()`. This prevents the `uBugCheck()` path that could crash foobar2000 when the View menu was opened. See `BUILD_VALIDATION_0.8_ALPHA3_L.md`.

# foo_sacd_dlna

Native DSD UPnP/DLNA Media Server component for [foobar2000](https://www.foobar2000.org/).

> **Status: Alpha / development build**
>
> This repository contains the current source tree for `foo_sacd_dlna` v0.8 Alpha 3 O. It is not a released foobar2000 component.

**Build status:** Alpha 3 I was confirmed by the maintainer as compiling successfully and running on Windows **Debug x64**, with `pfc` built as **Debug FB2K x64**, using the foobar2000 SDK 2025-03-07, MSVC v142 and the WTL headers documented in this repository. Alpha 3 M contains the subsequent roadmap integration and hardening and requires a fresh Windows rebuild. Hardware playback validation against the exact T+A SDX 3100 HV firmware remains a separate step.

## Alpha 3 M — code audit / hardening (historical baseline)

Alpha 3 M is a source-level robustness pass over the Alpha 3 L tree. It hardens server lifecycle recovery, HTTP stream-limit handling, cache manifest validation, cache-size reporting, live error reporting, network diagnostic synchronization, prefetch worker state, local-IP access, and source-extension normalization.

**Build status:** Alpha 3 I remains the latest user-confirmed revision compiled without errors and working at runtime. Alpha 3 M requires a fresh Windows/MSVC v142 rebuild.

## Buffer and UPnP/DLNA diagnostics UI — Alpha 3 J

Alpha 3 J adds a real-time **SACD DLNA Status / Diagnostics** panel. It refreshes every 500 ms and shows server DSD read-ahead, streaming state, measured TX rate, HTTP/SSDP readiness and observed network presence.

The buffer indicator is explicitly the **server-side read-ahead reserve**, not the T+A SDX's internal playback buffer. During a stream it can report `READY / FULL RESERVE`, `DRAINING / HEALTHY`, `LOW / REFILL NOT AVAILABLE` or `DEPLETED / RISK OF UNDERRUN`.

Network diagnostics distinguish `SSDP NOTIFY sent`, `HTTP self-test`, `SSDP self-probe` and remote `NETWORK VISIBILITY`. The UI reports `CONFIRMED / REMOTE SSDP M-SEARCH` or `CONFIRMED / REMOTE HTTP` only after a non-local peer actually reaches the server.

**Enable Debug Diagnostics** and **Run Network Probe** are available from Preferences and from **View → SACD DLNA**. The probe checks `device.xml`, `ContentDirectory.xml`, `ConnectionManager.xml` and performs an SSDP MediaServer `M-SEARCH`.

## What it is

`foo_sacd_dlna` is designed to expose the DSD part of a foobar2000 Music Library over UPnP/DLNA to compatible network audio renderers.

The initial target is the **T+A SDX 3100 HV**, with the following policy:

- DSD only on the network path.
- No DSD → PCM conversion by `foo_sacd_dlna`.
- No DoP network output.
- Native `DSF` / `DFF` delivery where supported by the renderer.
- SACD ISO playback depends on the separately installed **Super Audio CD Decoder (`foo_input_sacd`)**.
- The component uses foobar2000's public decoder interface rather than loading private `foo_input_sacd` DLL entry points.

The foobar2000 SDK 2025-03-07 documents `input_flag_dop` as the decoder flag for requesting DSD decoders to provide DSD as DoP; `foo_sacd_dlna` uses that public interface and unwraps the DSD bits before creating a DSF stream. ([foobar2000 SDK changelog](https://www.foobar2000.org/changelog-sdk))

## Main features in v0.8 Alpha 3 J

### foobar2000 integration

- Dedicated **Preferences → Tools → SACD DLNA** page.
- `View → SACD DLNA` main-menu group with enable/share/status/DSD-Processor commands.
- Optional **SACD DLNA Status** UI element.
- Visible `BROADCASTING / ACTIVE` state.
- Runtime check for `foo_input_sacd.dll` / Super Audio CD Decoder.
- Detected SACD Decoder version is shown when available.
- Configurable server name.
- Configurable HTTP port.
- Enable / disable DLNA broadcasting.
- Enable / disable DSD Music Library sharing.
- Refresh / clear shared-library state.
- Built-in help dialog.
- Link to foobar2000 Music Library preferences.

### Music Library

The shared tree is organised as:

```text
Artists
└── Artist
    └── Album
        └── Track
```

The component keeps references to foobar2000 media handles rather than making a permanent duplicate of the Music Library.

Supported DSD-oriented source families in this development build include:

- `.iso` (SACD ISO, decoded through `foo_input_sacd`)
- `.dsf`
- `.dff`

### Real renderer workflow

The current alpha implements the MediaServer-side renderer workflow rather than only a test HTTP endpoint:

```text
T+A SDX 3100 HV
      │
      ├── SSDP discovery
      ├── Device Description
      ├── ContentDirectory Browse/BrowseMetadata
      ├── ConnectionManager GetProtocolInfo
      │
      └── HTTP GET /media/<id>.dsf
                     │
                     └── DSF / SACD ISO → foo_input_sacd → DSF cache
```

The server also exposes a live `/status` page and a `SACD DLNA Status` UI element for discovery state, active transmission, TX rate, DSD rate, active client and detected T+A renderer.

### UPnP / DLNA

- SSDP discovery.
- UPnP MediaServer device description.
- ContentDirectory service.
- ConnectionManager service.
- `Browse` support for artist / album / track navigation.
- HTTP media serving.
- HTTP `Range` support for media delivery.
- Album-art serving when artwork is available through foobar2000.
- Diagnostic status page.
- Persistent/invalidation-aware DSF cache manifests.
- Renderer-aware MIME/protocolInfo selection using ConnectionManager capabilities.
- `BrowseMetadata`, `GetSystemUpdateID`, DIDL-Lite metadata and pagination.
- Persistent artwork cache with JPEG/PNG/WebP/GIF/BMP/TIFF detection.
- Bounded concurrent media requests and request cancellation.
- Media Library callback tracking with debounced refreshes.
- Optional timestamped network diagnostics/logging.
- Windows GitHub Actions build and smoke-test workflow.

### DSD

Current network target formats:

| Format | Network path |
|---|---|
| DSD64 | Native DSD / DSF |
| DSD128 | Native DSD / DSF |
| DSD256 | Native DSD / DSF |

For SACD ISO input, the intended path is:

```text
SACD ISO
   ↓
foo_input_sacd
   ↓
foobar2000 public input_decoder API
   ↓
DoP container inside foobar2000
   ↓
DSD bit extraction
   ↓
DSF
   ↓
HTTP / UPnP / DLNA
   ↓
T+A SDX 3100 HV
```

The DoP stage above is an **internal transport between the foobar2000 decoder interface and this component**. It is not sent to the network renderer.

## T+A SDX 3100 HV

T+A currently lists the SDX 3100 HV Streaming Client with `DFF` and `DSF` support and DSD64/DSD128/DSD256 native streaming. The unit's USB input supports higher DSD rates separately, including DSD512 and DSD1024, but those higher rates are not listed as Streaming Client rates on the current product page. ([T+A SDX 3100 HV](https://www.ta-hifi.de/en/audiosystems/hv-series/sdx-3100-reference-streaming-pre-dac/))

This project therefore targets **DSD64/128/256 over DLNA** first. Exact `protocolInfo`, MIME, range and renderer-specific behaviour still need validation against the firmware installed on the target SDX 3100 HV.

## Requirements

- Windows x64.
- foobar2000 x64.
- Visual Studio 2022 with the required C++ workload for building.
- foobar2000 **SDK 2025-03-07**.
- Super Audio CD Decoder (`foo_input_sacd.dll`) for SACD ISO/DSD decoding.
- A network player / renderer compatible with the implemented UPnP/DLNA profile.

The official foobar2000 SDK page currently lists **SDK 2025-03-07** and says its included project files target Visual Studio 2019/2022. ([foobar2000 SDK](https://www.foobar2000.org/SDK))

The Super Audio CD Decoder project currently publishes `foo_input_sacd-2.0.25.zip` (updated June 2026), with support for SACD ISO, DSDIFF and DSF. 


## Hardware and Network Requirements

### Minimum hardware

`foo_sacd_dlna` is a foobar2000 component and does not require a dedicated GPU.

For a practical Windows installation:

| Component | Minimum | Recommended |
|---|---|---|
| CPU | 2 physical cores / 4 threads | 4+ physical cores |
| RAM | 4 GB | 8 GB or more |
| System drive | SSD preferred | SSD |
| Music storage | HDD/SSD/NAS | SSD for the SACD/DSF cache |
| Network adapter | 100 Mbps Ethernet | 1 Gbps Ethernet |
| GPU | Not required | Integrated graphics is sufficient |
| OS | Windows 7 or newer | Current 64-bit Windows |
| foobar2000 | 64-bit recommended | Current 64-bit version |
| `foo_input_sacd` | **Required for SACD ISO** | Current version |

foobar2000's current official Windows requirements are Windows 7 or newer. The component itself does not use GPU acceleration. 

### DSD network bandwidth

Native DSD is intentionally not compressed by this component.

Approximate audio payload rates for stereo DSD are:

| Format | DSD clock | Approx. audio payload |
|---|---:|---:|
| DSD64 | 2.8224 MHz | 5.64 Mbit/s |
| DSD128 | 5.6448 MHz | 11.29 Mbit/s |
| DSD256 | 11.2896 MHz | 22.58 Mbit/s |

These figures are the DSD audio payload only; TCP/IP, HTTP and DLNA/UPnP overhead add additional traffic.

A 100 Mbps wired network is therefore technically sufficient for DSD256, but **1 Gbps Ethernet is strongly recommended** when the goal is reliable playback on a busy home network.

### Recommended network topology

For the most stable DSD256 streaming:

```text
                 1 Gbps Ethernet
                       │
                       ▼
              ┌────────────────┐
              │ Gigabit switch │
              └───────┬────────┘
                      │
              ┌───────┴────────┐
              │                │
              ▼                ▼
       Windows PC /       T+A SDX 3100 HV
       foobar2000
```

The PC and SDX should preferably be connected by Ethernet to the same switch/router.

5 GHz Wi-Fi can provide enough throughput for DSD256, but wired Ethernet is preferred because it provides more predictable latency and is less affected by radio interference and shared-medium congestion. The SDX 3100 HV provides 10/100/1000 Base-T Ethernet as well as Wi-Fi. 

### Network requirements by DSD rate

| Network | DSD64 | DSD128 | DSD256 |
|---|---|---|---|
| 100 Mbps Ethernet | ✓ | ✓ | ✓* |
| 1 Gbps Ethernet | ✓ | ✓ | **Recommended** |
| 2.4 GHz Wi-Fi | ✓* | Possible* | Not recommended |
| 5 GHz Wi-Fi | ✓ | ✓ | Possible* |

`*` Actual stability depends on other network traffic, signal quality, retransmissions, switch/router performance and storage performance.

### Stability Mode

For congested networks, enable **Stability Mode**.

The default configuration uses a 15-second read-ahead/cache strategy. It separates SACD decoding from network delivery so that short network slowdowns do not immediately interrupt playback.

For DSD256, 15 seconds of raw stereo DSD represents approximately 42.3 MB of audio payload.

Stability Mode cannot compensate for a sustained network throughput below the required DSD bitrate. In that situation, use Gigabit Ethernet and/or reduce simultaneous network traffic.

### Storage and caching

When an SACD ISO is used, the component can create a DSF cache before/while preparing delivery. An SSD is recommended for the cache because repeated reads of large DSD files can otherwise compete with other disk activity.

A useful rule for local cache capacity is:

- DSD64: about 20 MB/minute
- DSD128: about 40 MB/minute
- DSD256: about 81 MB/minute

These are approximate raw stereo DSD figures; filesystem and DSF/container overhead are additional.

### T+A SDX 3100 HV

The SDX 3100 HV documentation specifies LAN at 10/100/1000 Base-T and lists DFF/DSF and DSD64, DSD128 and DSD256 for the Streaming Client. Its DAC itself supports higher native DSD rates through other input paths, but that should not be confused with the documented network streaming formats. 


## Build

This repository intentionally does **not** redistribute the foobar2000 SDK.

Download the official SDK and place this project in the SDK tree next to the sample components:

```text
SDK-2025-03-07/
├── foobar2000/
│   ├── SDK/
│   ├── shared/
│   └── foo_sacd_dlna/
├── pfc/
└── libPPUI/
```

Then:

1. Open `foobar2000/foo_sacd_dlna/foo_sacd_dlna.sln` in Visual Studio 2022.
2. Select `Release` + `x64`.
3. Build the project.
4. Install the resulting component in foobar2000 for testing.

### Important

The source package was prepared in an environment without MSVC/Visual Studio. **The current Alpha source has not been compiled here.** Treat the project as a development source drop and expect further SDK/compiler fixes during the alpha stage.

## Installation for testing

1. Install foobar2000 x64.
2. Install the Super Audio CD Decoder (`foo_input_sacd`).
3. Install the compiled `foo_sacd_dlna` component.
4. Open **File → Preferences → Tools → SACD DLNA**.
5. Confirm that the status shows:

```text
foo_input_sacd: INSTALLED
```

6. Enable **DLNA broadcasting**.
7. Enable **Share DSD Music Library**.
8. On the network player, open the UPnP/DLNA server:

```text
foobar2000 SACD DSD
```

## Runtime status

The component exposes the following status information in Preferences and in the optional UI element:

```text
SACD DLNA:  BROADCASTING / ACTIVE
foo_input_sacd:  INSTALLED  <version>
Music Library:  SHARING  (<N> DSD tracks)
Server: foobar2000 SACD DSD
HTTP port: 8192
```

`BROADCASTING / ACTIVE` means the component's HTTP and SSDP services are running. It does **not** by itself prove that a renderer has accepted or is currently playing a stream.

## SACD ISO caching

SACD ISO content is decoded to a temporary DSF cache when a network request requires the track.

This is deliberate: the component is not intended to permanently convert the entire SACD collection to DSF files.

Native DSF/DFF files can be served directly without this SACD-ISO decode/cache stage.

## Default network settings

- HTTP server: `TCP 8192`
- SSDP multicast: `239.255.255.250:1900`

Windows Firewall may require an inbound rule for foobar2000 on the private network.

## Security / network scope

This alpha component is intended for a **trusted local network**. The HTTP service is not designed as an Internet-facing server and currently has no authentication layer.

Do not expose the DLNA HTTP port directly to the public Internet.

## Architecture

```text
                    FOOBAR2000
                        │
              ┌─────────┴─────────┐
              │                   │
       Music Library        foo_input_sacd
              │                   │
              └─────────┬─────────┘
                        │
                        ▼
                 foo_sacd_dlna
                        │
          ┌─────────────┼─────────────┐
          │             │             │
        SSDP        ContentDir      HTTP
          │             │             │
          └─────────────┴─────────────┘
                        │
                        ▼
                 T+A SDX 3100 HV
```

## Project files

- `foo_sacd_dlna.vcxproj` — Visual Studio project.
- `foo_sacd_dlna.sln` — Visual Studio solution.
- `dlna_server.*` — SSDP, UPnP and HTTP server.
- `sacd_decode.*` — DSD decode bridge using foobar2000's public input API.
- `dsf_writer.*` — DSF writer/cache support.
- `preferences.cpp` — dedicated preferences page.
- `mainmenu.cpp` — Tools menu integration.
- `ui_element.cpp` — `SACD DLNA Status` UI element.
- `config.*` — persistent component configuration and SACD Decoder detection.
- `HELP.md` — built-in help text / user notes.

## Roadmap

### Completed in Alpha 3 J

- More complete UPnP `Browse` / `BrowseMetadata` pagination.
- Renderer-specific `protocolInfo` negotiation using the exact compatible Sink token when available.
- Deterministic track ordering for gapless testing.

### Roadmap status in Alpha 3 J

- More robust gapless preparation with server-side next-track prefetch ✅
- Complete DIDL-Lite track metadata and duration/resolution attributes ✅
- Persistent artwork cache with JPEG/PNG/WebP/GIF/BMP/TIFF detection ✅
- Bounded concurrent HTTP clients with cancellation ✅
- Persistent, invalidation-aware SACD→DSF and DSP→DSF cache ✅
- Media Library callbacks and `GetSystemUpdateID` change tracking ✅
- Console + `network.log` diagnostics with explicit remote network-visibility state ✅
- Windows GitHub Actions build/package workflow ✅
- T+A SDX discovery, ConnectionManager protocol negotiation and hardware validation checklist ✅
- Remaining external validation: rebuild on Windows and test the exact SDX 3100 HV firmware/network path

## Contributing

Issues, protocol traces, build errors and renderer compatibility reports are especially useful during the alpha stage.

For hardware testing, include:

- foobar2000 version
- `foo_input_sacd` version
- Windows version
- T+A SDX 3100 HV firmware version
- renderer connection type (LAN / Wi-Fi)
- the relevant `foo_sacd_dlna` console output

Please do not upload copyrighted music files or SACD ISO images to the issue tracker.

## Disclaimer

`foo_sacd_dlna` is experimental software. It can contain bugs, incomplete UPnP/DLNA behaviour or compatibility problems with particular renderer firmware. Use test material first and keep backups of your music library.

## View → SACD DLNA / status window

Alpha 3 J adds an explicit **Open SACD DLNA Status** command and **Configure DSD Processor...** entry under **View → SACD DLNA**. The status popup also has explicit initial dimensions so it opens as a usable window. The `SACD DLNA Status` UI element remains available through foobar2000's UI element editing system.

## Visibilidade na rede

O estado de rede distingue agora: `SSDP NOTIFY` enviado, `HTTP/SSDP self-test` local, e **visibilidade remota confirmada**.

`NETWORK VISIBILITY: CONFIRMED / REMOTE SSDP M-SEARCH` significa que outro equipamento da LAN enviou um M-SEARCH ao servidor. `NETWORK VISIBILITY: CONFIRMED / REMOTE HTTP` significa que outro IP abriu uma ligação TCP HTTP ao servidor. `LOCAL SSDP READY / WAITING FOR REMOTE PEER` significa que a pilha local está activa, mas ainda não houve tráfego iniciado por outro dispositivo.

Um self-probe local, por si só, não prova que todos os switches, VLANs, APs ou firewalls da rede permitem descoberta externa.

## Live DLNA / T+A monitoring

Version 0.5 adds live transport monitoring. The UI deliberately separates two states:

- **DLNA discovery: BROADCASTING / ACTIVE**: SSDP announcements and discovery are enabled.
- **Audio stream: ACTIVE / TRANSMITTING**: a DLNA renderer is actively receiving audio over HTTP.
- **TX speed**: measured TCP payload rate sent by `foo_sacd_dlna`.
- **DSD rate**: nominal DSD stream rate when it can be determined (DSD64/128/256).
- **DLNA client**: current renderer IP and known device identity.
- **T+A SDX**: UPnP/SSDP discovery attempts to identify the T+A renderer and shows `DETECTED / STREAMING` when its IP matches the active audio client.

Audio is not broadcast as a UDP stream. SSDP is used for discovery; the actual music bytes are delivered to the renderer over HTTP.

## Stability Mode (V0.7)

Stability Mode decouples SACD ISO → DSD conversion from the network delivery path. ISO tracks are converted to a persistent DSF cache and transmission starts only after the DSD file is ready. A configurable 5–60 second read-ahead and a larger TCP send buffer can be used before transmission.

This is useful for short disk/network fluctuations. No server-side buffer can guarantee uninterrupted playback when sustained network throughput is below the bitrate required by the selected DSD rate.

Approximate stereo payload rates: DSD64 = 5.64 Mbit/s; DSD128 = 11.29 Mbit/s; DSD256 = 22.58 Mbit/s.

## Why the buffer is useful

The component never converts DSD to PCM to reduce network bandwidth. For SACD ISO, the selected track is decoded to native DSD and written to a persistent DSF cache before the T+A is allowed to download it. This decouples the potentially variable SACD decoding speed from the network path.

Stability Mode additionally primes 5–60 seconds of DSF read-ahead and increases the TCP send buffer. The status panel shows the selected DSD rate, required payload bitrate, current TX rate and TX/required headroom.

This can absorb short network dips, but it cannot make a link that is continuously slower than the DSD payload rate play without interruption.

## Current Alpha roadmap

The current alpha focuses on real renderer interoperability and diagnostics:

- complete `Browse` / `BrowseMetadata` and pagination ✅
- renderer-specific DSD `protocolInfo` negotiation using the exact negotiated Sink token where possible ✅
- deterministic track order and duration metadata for gapless testing ✅
- richer DIDL-Lite metadata and album art
- concurrent HTTP clients with cancellation
- persistent, invalidation-aware SACD→DSF cache
- Media Library callbacks and `GetSystemUpdateID`
- verbose Console + `network.log` diagnostics
- Windows GitHub Actions build packaging
- explicit T+A SDX 3100 HV firmware validation checklist

Gapless playback is deliberately marked as **renderer/firmware dependent** until it is tested on the exact SDX firmware.

## Roadmap

See [`ROADMAP.md`](ROADMAP.md) for the complete implementation and external-validation matrix.

## Documentation

- `HELP.md` — field-by-field help and troubleshooting.
- `EXAMPLES.md` — practical playback and diagnostic examples.
- `NETWORK_REQUIREMENTS.md` — hardware/network requirements.
- `PREFERENCES_FIELDS.md` — preferences quick reference.
- `HARDWARE_VALIDATION.md` — T+A SDX 3100 HV firmware validation matrix.
- `DLNA_TRACE_EXAMPLE.md` — expected UPnP/DLNA request sequence.
- `NETWORK_DIAGNOSTICS_UI_0.8_ALPHA3_G.md` — buffer/read-ahead UI and network validation details.
- `BUILD_VALIDATION_0.8_ALPHA3_E.md` — recorded successful Windows build validation for this version.

## Real DLNA / renderer validation

The MediaServer path now separates SSDP discovery from real HTTP media transfer and records live renderer/TX status. Renderer capabilities are queried through UPnP `ConnectionManager::GetProtocolInfo` where available. Final T+A compatibility remains firmware-specific and must be validated on the exact SDX 3100 HV unit. See `HARDWARE_VALIDATION.md` and `tools/ta_sdx_probe.py`.

The Preferences page also provides **Clear DSF Cache** for invalidating generated DSF/manifests/artwork without touching source music.

See `PROTOCOL_COMPATIBILITY.md` for renderer negotiation details and `HARDWARE_VALIDATION.md` for exact-firmware testing.

## References

- [foobar2000 SDK](https://www.foobar2000.org/SDK)
- [foobar2000 SDK 2025-03-07 changelog](https://www.foobar2000.org/changelog-sdk)
- [T+A SDX 3100 HV specifications](https://www.ta-hifi.de/en/audiosystems/hv-series/sdx-3100-reference-streaming-pre-dac/)
- [Super Audio CD Decoder (`foo_input_sacd`)](https://sourceforge.net/projects/sacddecoder/files/foo_input_sacd/)

## Build validation — 0.8 Alpha 3 J

Alpha 3 J is a subsequent source revision and **was not build-validated in this environment**. The latest user-confirmed Windows build/runtime baseline remains Alpha 3 I.
See `BUILD_VALIDATION_0.8_ALPHA3_I.md`.

Build environment recorded for this validation:

```text
foobar2000 SDK: 2025-03-07
Platform:       x64
Configuration:  Debug
Toolset:        MSVC v142
WTL:            <SDK root>\<WTL folder>\include
```

This confirms a clean source/build configuration for the documented development build. It does **not** by itself certify runtime compatibility, gapless playback, or exact-firmware behaviour of the T+A SDX 3100 HV.

See [`BUILD_VALIDATION_0.8_ALPHA3_E.md`](BUILD_VALIDATION_0.8_ALPHA3_E.md) for the recorded validation details.

## Compilação / Build

See [`BUILD.md`](BUILD.md) for the complete Windows build guide, software/hardware requirements, Visual Studio setup, SDK configuration, testing and troubleshooting.


## Optional DSD Processor integration

The DLNA server can optionally run the installed **DSD Processor** DSP (`foo_dsd_processor`) on the audio before network delivery. This is intentionally separate from foobar2000's normal playback DSP chain.

The user keeps control of the DSD Processor preset through its normal configuration window. The private preset is stored by `foo_sacd_dlna` and its fingerprint is included in DSF cache invalidation.

Typical use:

```text
PCM → DSD Processor → DSD128 → DSF → DLNA → SDX
DSD256 → DSD Processor → DSD128 → DSF → DLNA → SDX
```

The first path can convert PCM sources to DSD for the T+A. The second can reduce DSD256 network bandwidth by configuring the DSD Processor to output DSD128. The default mode remains native DSD/bypass.

The current `foo_dsd_processor` documentation describes PCM→DSD and DSD sample-rate conversion. This DLNA integration intentionally requires the DSP result to remain DSD/DoP so that network delivery stays native-DSD. DSD→PCM is not silently enabled by this option.

- [`DSP_PROCESSOR.md`](DSP_PROCESSOR.md) — optional DSD Processor integration and configuration.

## Windows build note

The component targets **MSVC v142**. The foobar2000 SDK helper layer also
requires **WTL** headers (including `atlapp.h`). See [`WTL_SETUP.md`](WTL_SETUP.md)
for installation and Visual Studio configuration.

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
<SDK root>\<WTL folder>\include
```

See `BUILD.md`, `WTL_SETUP.md` and `V142_WTL_FIX.md` for the complete configuration.

## Alpha 3 J — live audio information

The SACD DLNA Status panel now shows the active music title/artist/album, source and output formats, source/output file size, DSD/PCM sample rate, channels and bit depth, effective network speed in x-realtime, and the active processing pipeline. The conversion label distinguishes native DSD (`NO CONVERSION`), SACD ISO decoding/cache, and DSD Processor output (`DSP OUTPUT / CACHED` or `DSP CONVERTING`).

## Alpha 3 J — Live audio information

The Status / Diagnostics panel now shows the active music title/artist/album, source and output format, source/output file size, sample rate/resolution, channel count and bit depth, measured TX speed, effective x-realtime rate, and the current processing/conversion pipeline. During ISO cache creation or DSD Processor processing, the preparation state is shown immediately.

### View → SACD DLNA menu hardening — Alpha 3 K

The complete View → SACD DLNA command set was reviewed. Library sharing is now a true toggle, the preferences command opens the dedicated SACD DLNA page directly, and refresh/clear-library/clear-cache actions are available from the same menu. DSD Processor toggling now only re-indexes an already shared/running DLNA library.
## Alpha 3 N — Windows discovery hardening

Improved Windows UPnP/DLNA discovery compatibility: LAN-interface selection for the advertised LOCATION, DLNA device namespace/description, SSDP service announcements and service-type M-SEARCH responses. Added explicit advertised LOCATION diagnostics. Windows Explorer discovery remains dependent on the Windows SSDP/Function Discovery stack and firewall configuration.



### Alpha 3 P — WTL discovery fix

A descoberta do WTL foi corrigida para evitar referências a listas de itens dentro de `Condition`.


### Alpha 3 Q — WTL relocation / MSBuild fix

Alpha 3 Q removes the invalid MSBuild item-list-to-property conversion from WTL discovery. It also adds an SDK-root `Directory.Build.targets` overlay so WTL headers are injected into referenced projects such as libPPUI and foobar2000_sdk_helpers. The `tools/install_wtl_support.ps1` script auto-detects any WTL folder containing `include\atlapp.h`, independent of the folder name.

## Alpha 3 S — compile fixes
Alpha 3 S fixes the live-status mutex constness and signed/unsigned cache-manifest comparison errors found in the user's MSVC build. It also includes a PowerShell patcher to propagate the relocatable WTL include to `libPPUI` and `foobar2000_sdk_helpers`.
