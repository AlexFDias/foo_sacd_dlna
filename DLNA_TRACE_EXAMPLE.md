# Expected UPnP/DLNA Trace

```text
1. Renderer discovery
   SDX -> 239.255.255.250:1900 M-SEARCH

2. MediaServer description
   SDX -> GET /device.xml

3. Service descriptions
   SDX -> GET /ContentDirectory.xml
   SDX -> GET /ConnectionManager.xml

4. Content browsing
   SDX -> POST /ctl/ContentDirectory
          Browse ObjectID=0
   SDX -> Browse ObjectID=artists
   SDX -> Browse ObjectID=artist-<id>
   SDX -> Browse ObjectID=album-<id>

5. Track metadata
   SDX -> BrowseMetadata ObjectID=track-<id>

6. Renderer capability query
   SDX -> POST /ctl/ConnectionManager
          GetProtocolInfo

7. Audio delivery
   SDX -> HEAD /media/<id>.dsf
   SDX -> GET /media/<id>.dsf
          Range: bytes=...

8. Monitoring
   foo_sacd_dlna -> reports renderer IP, DSD rate, bytes sent, TX rate and stream duration.
```

For SACD ISO, the media request causes `foo_input_sacd` to decode the selected subsong into a validated persistent DSF cache before delivery.

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
```

See `BUILD.md`, `WTL_SETUP.md` and `V142_WTL_FIX.md` for the complete configuration.
