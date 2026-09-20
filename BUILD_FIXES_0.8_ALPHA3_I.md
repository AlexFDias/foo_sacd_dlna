
# Build Fix — v0.8 Alpha 3 I

The Alpha 3 H build failed because `dlna_server.cpp` referenced three `SacdDlnaServer` members that were not declared in `dlna_server.h`:

- `m_streamChannels`
- `m_streamBitsPerSample`
- `m_streamDuration`

Alpha 3 I declares these members with the types used by the existing status code:

- `uint32_t m_streamChannels`
- `uint32_t m_streamBitsPerSample`
- `double m_streamDuration`

No linker or SDK dependency changes are required for this fix.
