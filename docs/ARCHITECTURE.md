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


### DVD-Audio cache availability

`ensureCachedFlac()` must not use `dvda_plugin_installed()` as a hard prerequisite for serving or generating a cache. That probe enumerates `componentversion` services; it is diagnostic metadata, not the decoder API. DVD-Audio conversion itself goes through `input_entry::g_open_for_info_read()` / `input_entry::g_open_for_decoding()`. Existing validated caches remain usable when the component probe returns no version.
### Media HTTP diagnostics v4

The media endpoint distinguishes an unknown media ID (`404`) from a known item whose DVD-Audio/DSF preparation failed (`503`). Network diagnostics record the preparation reason so renderer logs can identify whether the failure is ContentDirectory/media-ID mapping or cache/decoder generation.



### DVD-Audio: PCM authoritative format

The DVD-Audio FLAC path opens the decoder before configuring libFLAC and uses the first decoded PCM chunk as the authoritative sample-rate/channel layout. This avoids relying exclusively on static `file_info` metadata for DVD-Audio program variants such as downmix and C/LFE tracks. Subsequent chunks must keep the same PCM format; a mismatch is reported as a conversion error.

### v7 Range/stream limiter behavior

A single renderer may use multiple HTTP Range connections during seeking or prefetch. These connections are treated as one logical active stream when they originate from the same already-streaming peer, so the Max Streams limit does not reject a renderer's own seek/prefetch connection with HTTP 503.

## v8 — DVD-Audio decoder priming-chunk fix

DVD-Audio tracks may emit one or more empty/setup PCM decoder runs before the first real block. The FLAC conversion path skips those runs and derives the libFLAC stream format from the first non-empty PCM block; a 64-run guard prevents an infinite loop.
