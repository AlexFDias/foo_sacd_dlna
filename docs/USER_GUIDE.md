# foo_sacd_dlna — User Guide

## Installation

1. Install a compatible 64-bit foobar2000.
2. Install `foo_input_sacd` if SACD ISO/DSD SACD support is required.
3. Install `foo_input_dvda` if DVD-Audio support is required.
4. Install `foo_dsd_processor` only if DLNA-side DSP processing is required.
5. Install the component built from this source tree.

## Initial configuration

Open:

`File → Preferences → Tools → SACD DLNA`

Use **Settings** to enable the server and configure the shared library.

## Recommended settings

For a wired home network:

- Enable DLNA.
- Enable Share Music Library.
- Use a free HTTP port, with the default documented by the current tree being 8192.
- Keep Max streams at 2 unless there is a specific need for concurrent playback.
- Enable Stability mode for DSD streaming where short network/storage stalls are a concern.
- Use 15 seconds as the initial pre-buffer value.

## DVD-Audio

Install `foo_input_dvda`, enable the component, and include the required source extension in **Shared formats**. DVD-Audio is decoded to PCM and served as cached lossless FLAC.

## Diagnostics

Use **Maintenance → Run Network Probe** to check the local UPnP/DLNA endpoints. Enable **Network logging** or **Debug Diagnostics** when investigating discovery or streaming problems.

Use **Status** to distinguish server state, clients, active streams, renderer detection, buffer state and network visibility.

## Clearing state

**Clear shared Music Library** removes the server's published library state; it does not delete source files.

**Clear persistent cache** removes generated DSF/FLAC/artwork cache data; it does not modify source files.
