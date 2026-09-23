# Media ID stability fix

## Problem observed

VLC requested valid DLNA resources such as `/media/95.flac`, `/media/135.flac`,
`/media/107.flac` and `/media/100.flac`, but the server returned HTTP 404.

The previous fix kept IDs stable only while the component process remained alive.
`m_nextId` was reset when foobar2000 restarted. Because the UPnP UUID remains
stable, a control point can retain a DIDL resource URL from the previous process
and request it after rediscovery.

## Fix

Media IDs are now deterministic hashes of:

- normalized foobar2000 source path;
- subsong index.

The ID is therefore stable across library refreshes and component/foobar2000
restarts. Extremely unlikely 32-bit hash collisions are resolved without using
process-local counters.

HTTP delivery also no longer requires a live `metadb_handle` before generating a
DVD-Audio FLAC cache; the cache is keyed by source path/subsong.

## Scope

This change affects media resource IDs only. UPnP object IDs, playlist IDs and
the fixed server UUID are unchanged.
