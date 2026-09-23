# foo_sacd_dlna — Documentation Index

This directory contains the consolidated project documentation. The source tree no longer carries obsolete per-revision build/audit notes.

## Authoritative documents

- `DOCUMENTACAO.pt-PT.md` — documentação técnica e de utilização consolidada, em Português (Portugal).
- `DOCUMENTATION.md` — consolidated English documentation.
- `ARCHITECTURE.md` — architecture and data-flow reference.
- `USER_GUIDE.md` — installation, configuration and operating guide.
- `DEVELOPER_GUIDE.md` — build, source tree, testing and contribution guidance.
- `VALIDATION_STATUS.md` — explicit distinction between source-level features, historical build validation and hardware validation still required.
- `../FLAC_RUNTIME.md` — official libFLAC 1.5.x integration and DVD-Audio cache validation.

### v7 Range/stream limiter behavior

A single renderer may use multiple HTTP Range connections during seeking or prefetch. These connections are treated as one logical active stream when they originate from the same already-streaming peer, so the Max Streams limit does not reject a renderer's own seek/prefetch connection with HTTP 503.
