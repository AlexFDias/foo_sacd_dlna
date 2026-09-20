# Build validation — foo_sacd_dlna 0.8 Alpha 3 K

Alpha 3 K is based on the user-validated Alpha 3 I runtime/build and contains menu-only hardening plus administrative command wiring.

The previous Alpha 3 I was confirmed by the maintainer as compiling without errors and functioning in foobar2000, including the SACD DLNA Status UI.

Alpha 3 K changes still require a fresh Windows/MSVC v142 rebuild before being marked build-validated.

## Menu review
- View → SACD DLNA → Enable DLNA broadcasting: toggle server/SSDP.
- View → SACD DLNA → Share DSD Music Library: true on/off toggle.
- View → SACD DLNA → Enable DLNA DSD Processor: toggle DSP integration; only refreshes a currently shared/running library.
- View → SACD DLNA → Open SACD DLNA Status: opens the live status UI.
- View → SACD DLNA → Run Network Diagnostics: local UPnP/SSDP checks.
- View → SACD DLNA → Enable Debug Diagnostics: toggles diagnostics.
- View → SACD DLNA → Configure DSD Processor...: opens the installed DSP configuration.
- View → SACD DLNA → Refresh DSD Music Library: re-indexes and shares.
- View → SACD DLNA → Stop Sharing Music Library: clears the published library without stopping the server.
- View → SACD DLNA → Clear Persistent Cache: removes generated cache files only.
- View → SACD DLNA → Open SACD DLNA Preferences: opens the dedicated Tools → SACD DLNA page directly.
- View → SACD DLNA → Help: shows basic requirements/usage.
