# DLNA DSD Processor integration

`foo_sacd_dlna` can optionally run the installed `foo_dsd_processor` as a private DSP chain for network delivery. The component uses the public foobar2000 DSP API (`dsp_entry` + `dsp_manager`) and does not modify the user's normal playback DSP chain.

## Modes

### Native DSD / bypass (default)

```text
DSD source → DSF → DLNA
```

No DSP processing is performed.

### DSD Processor enabled

```text
PCM source → DSD Processor → DoP/DSD → DSF → DLNA
DSD source → DSD Processor → DoP/DSD → DSF → DLNA
```

The DSD Processor preset determines the conversion/resampling rules. The plugin rejects the result unless it is valid DSD/DoP and can be written as DSF.

## Why this can help network stability

DSD256 is roughly 22.58 Mbit/s of stereo DSD payload, while DSD128 is roughly half of that. Configuring the DSD Processor to output DSD128 can therefore reduce the sustained network payload while keeping the network path DSD.

This is a bandwidth/compatibility option, not a claim that DSD128 is sonically equivalent to the source DSD256.

## Configuration

1. Install `foo_dsd_processor`.
2. Open `File → Preferences → Tools → SACD DLNA`.
3. Enable `Process DLNA audio through DSD Processor`.
4. Click `Configure DSD Processor...`.
5. Configure the desired PCM→DSD and/or DSD-rate mappings.
6. Save/apply the SACD DLNA preferences.

The stored DSP preset fingerprint forms part of the DSF cache key. Changing the DSD Processor preset therefore causes old converted cache entries to be ignored automatically.

## Important limitation

The upstream `foo_dsd_processor` documentation describes PCM→DSD and DSD-rate conversion. The SACD decoder separately documents DSD→PCM conversion. This component keeps the network result DSD-only, so a PCM result from the DSP is rejected rather than silently switching the renderer to PCM.

References:
- https://sourceforge.net/projects/sacddecoder/files/foo_dsd_processor/
- https://sourceforge.net/projects/sacddecoder/files/foo_input_sacd/

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
```

See `BUILD.md`, `WTL_SETUP.md` and `V142_WTL_FIX.md` for the complete configuration.
