# foo_sacd_dlna

UPnP/DLNA MediaServer component for foobar2000 with **native DSD** and **DVD-Audio → FLAC** support.

**Tree version:** `1.0.0`  
**Status:** Alpha / development  
**SDK:** foobar2000 SDK 2025-03-07  
**Platform:** Windows / foobar2000 x64

## Main features

- UPnP/DLNA MediaServer with SSDP, Device Description, ContentDirectory and ConnectionManager.
- Music Library tree with Artists, Albums, Genres, Folders and All Tracks.
- SACD ISO through the public `foo_input_sacd` interface, prepared for DSD/DSF delivery.
- Native DSF/DFF.
- DVD-Audio through `foo_input_dvda`, with a cached lossless 24-bit FLAC path for DLNA.
- Configurable **Shared formats** filter.
- Configurable **1–16 simultaneous stream** limit, default 2.
- Stability Mode and 5–60 second configurable read-ahead, default 15.
- Persistent audio/artwork caches with relevant-state invalidation.
- HTTP/SSDP diagnostics and `/status`.
- Optional `foo_dsd_processor` integration through the public DSP API.
- Native foobar2000 Preferences pages: **Status**, **Settings**, **Maintenance**.

## Preferences

```text
File → Preferences → Tools → SACD DLNA

SACD DLNA
├── Status
├── Settings
└── Maintenance
```

Current dialog resources:

| Page | Size |
|---|---:|
| Root | 390 × 180 |
| Status | 450 × 400 |
| Settings | 450 × 400 |
| Maintenance | 450 × 400 |

The component does not resize or manipulate the foobar2000 Preferences host window.

## Functional dependencies

- `foo_input_sacd` — SACD ISO/DSD SACD.
- `foo_input_dvda` — DVD-Audio.
- `foo_dsd_processor` — optional, only for DLNA DSP processing.

## Validation status

The historical record identifies **Alpha 3 I** as the latest revision explicitly confirmed by the maintainer as compiling and running on Windows Debug x64. Later revisions contain additional source changes and require a fresh Windows/MSVC v142 build before equivalent build validation can be claimed.

The current `1.0.0` tree should therefore be treated as an **Alpha source tree requiring a fresh build and validation of the exact revision** before release.

T+A SDX 3100 HV validation remains dependent on the exact hardware and firmware under test.

Real-world diagnostic logs from a foobar2000 + T+A SDX + VLC test session (see `docs/VALIDATION_STATUS.md`) show the DVD-Audio → FLAC validation and streaming-timeout logic working correctly; every observed failure traced back to `libFLAC.dll` not being deployed next to `foo_sacd_dlna.dll`, which is now surfaced as a startup Console warning instead of silent per-track `503`s. This is evidence from one test environment, not a substitute for the checklist in `RELEASE_CHECKLIST.md`.

## Documentation

See:

- `docs/DOCUMENTATION.md` — consolidated documentation entry point.
- `docs/ARCHITECTURE.md` — architecture.
- `docs/USER_GUIDE.md` — user guide.
- `docs/DEVELOPER_GUIDE.md` — build, tests and development.
- `docs/VALIDATION_STATUS.md` — validation status.
- `DOCUMENTATION_INDEX.md` — full documentation index.

The tree keeps only current source, build instructions, user/developer documentation and the consolidated changelog; obsolete revision-by-revision build/audit notes were removed.

## License

Original project code is released under the MIT License. Third-party dependencies remain under their respective terms.

See `docs/ARCHITECTURE.md` for the DVD-Audio FLAC pipeline, cache-availability, media-diagnostics and stream-limiter implementation notes, and `CHANGELOG.md` for their revision history.
