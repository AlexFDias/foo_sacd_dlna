# foo_sacd_dlna — Architecture Reference

## 1. High-level pipeline

```text
foobar2000 Music Library
        │
        ├── DSD/SACD ──> decoder ──> DSD bits ──> DSF cache ──┐
        │                                                      │
        ├── DVD-Audio ─> foo_input_dvda ─> PCM ─> FLAC cache ──┤
        │                                                      │
        └── native shared formats ─────────────────────────────┤
                                                               ▼
                                                        ContentDirectory
                                                               │
                                                        HTTP media server
                                                               │
                                      SSDP / UPnP / ConnectionManager
                                                               │
                                                               ▼
                                                            Renderer
```

## 2. Major modules

- `dlna_server.*`: owns HTTP/SSDP, UPnP SOAP actions, publication, caches and streaming state.
- `library_index.h`: maintains lookup structures used by Browse and media/artwork resolution.
- `sacd_decode.*`: public foobar2000 decoder integration for SACD/DSD.
- `dvd_audio_flac.*`: converts DVD-Audio decoder output to cached 24-bit FLAC.
- `dsf_writer.*`: creates DSF output from DSD data.
- `dsp_bridge.*`: optional DSD Processor integration.
- `client_registry.h`: client lifecycle and atomic stream admission control.
- `preferences.cpp`: native foobar2000 preference pages.
- `status.h`: immutable-style status snapshot structure shared by diagnostics/UI code.

## 3. SACD ISO flow

The component requests DSD-capable decoder output through the public foobar2000 decoder API. The internal DoP representation is unpacked into DSD bits and written as DSF. The ISO container itself is not exposed as the network audio payload for this path.

## 4. DVD-Audio flow

DVD-Audio is identified as a DVD-A source and decoded through `foo_input_dvda`. The resulting PCM is encoded into a cache file containing FLAC frames with 24-bit verbatim subframes. The cache manifest records decoder-version information so a decoder change can invalidate stale generated media.

The current implementation rejects more than eight channels because its FLAC channel mapping is not defined beyond that point.

## 5. Admission control

The stream limiter is separate from client accounting. A client may exist without holding an audio stream slot. A slot is acquired immediately before audio transmission and released on all terminal paths. Requests above the configured limit receive HTTP 503/Retry-After.

## 6. Cache invalidation

Generated media is keyed by source identity and the relevant processing/decoder state. SACD/DSP caches and DVD-Audio FLAC caches use manifests to prevent reuse after relevant changes.

## 7. Preferences lifecycle

The root Preferences page is a small summary. Three child `preferences_page_instance` pages provide the real controls. Each child is owned by the foobar2000 Preferences host. The component does not resize or subclass the host window.

Current resources:

```text
Root        390 x 180
Status      450 x 400
Settings    450 x 400
Maintenance 450 x 400
```
