# Code audit — foo_sacd_dlna 0.8 Alpha 3 M

## Review scope
Reviewed the Alpha 3 L source after the confirmed working Alpha 3 I build, focusing on lifecycle, HTTP/DLNA, cache validation, diagnostics, UI consistency and concurrency.

## Fixes applied
- Safe server restart after HTTP/SSDP startup failure; stale joinable worker threads and Winsock state are cleaned before a new start.
- `stop()` now performs cleanup even when `m_running` is already false.
- HTTP/SSDP ready flags are cleared on shutdown.
- HTTP client worker creation is failure-safe.
- `GET /media/...` now checks the concurrent-stream limit before sending a 200/206 response, avoiding invalid 200+503 responses.
- Cache manifest numeric fields use exact JSON-field matching rather than prefix matching.
- SACD decoder version is checked when validating both native SACD and DSP-generated caches.
- Local HTTP probe uses partial-send handling and can terminate on a known Content-Length rather than unnecessarily waiting for socket timeout.
- Header lookup now requires an exact header name before `:` and URL parsing rejects invalid TCP ports.
- Published source extensions are normalized to lowercase, avoiding uppercase `.DSF/.DFF/.ISO` metadata/path inconsistencies.
- Prefetch status reads are mutex-protected and completed aborter entries are removed; concurrent prefetch activity is counted.
- The live UI labels the server buffer as an estimated send-buffer/read-ahead reserve rather than implying visibility into the T+A renderer buffer.
- Cache-size scanning is throttled to a short cache instead of scanning the filesystem every 500 ms paint cycle.
- Runtime errors are retained in Status even when file logging is disabled.
- Device XML/SSDP version strings are updated to Alpha 3 M.
- Status double-click opens the dedicated SACD DLNA preferences page, consistent with the View menu.

## Remaining observations
- UPnP event subscription endpoints are advertised but not implemented; browsing/playback does not require them on the tested path, but this remains a protocol-completeness item.
- The server-side “buffer” remains a read-ahead/reserve indicator, not the internal buffer of the T+A renderer.
- The source supports IPv4 network discovery; IPv6 SSDP is not implemented.
- A full MSVC/v142 build must still be performed after these changes.
- UPnP event subscription endpoints are still advertised but not implemented.
- IPv6 SSDP is not implemented.
- Music Library re-indexing remains synchronous when explicitly requested from the menu/preferences and may be noticeable on very large libraries.
- The HTTP client worker vector retains completed `std::thread` wrapper objects until server stop; this is bounded by request count and does not retain running OS threads, but a future worker-pool/reaper can reduce long-session vector growth.
