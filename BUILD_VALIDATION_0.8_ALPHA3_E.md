# Build Validation — foo_sacd_dlna 0.8 Alpha 3 E

## Status

**PASS — compiled successfully without errors.**

The project maintainer compiled `foo_sacd_dlna` **v0.8 Alpha 3 E** on Windows in **Debug x64**. The complete solution finished successfully with no C++ compilation errors and no linker errors for the final `foo_sacd_dlna.dll`.

## Validated solution projects

| Project | Result |
|---|---|
| `foobar2000_component_client` | OK |
| `foobar2000_sdk_helpers` | OK |
| `pfc` | OK |
| `foobar2000_SDK` | OK |
| `libPPUI` | OK |
| `foo_sacd_dlna` | OK |

## Build configuration

```text
Configuration: Debug
Platform:     x64
Toolset:      MSVC v142
foobar2000:   SDK 2025-03-07
WTL:          SDK-2025-03-07\WTL\include
```

The `pfc` project is mapped to the foobar2000-specific configuration: 

```text
Debug   -> Debug FB2K
Release -> Release FB2K
```

This prevents `pfc-fb2k-hooks.cpp` from producing duplicate symbols already supplied by the SDK side. The official SDK `libPPUI` project is also included in the solution so that the final linker dependency is built automatically.

## What this validation proves

This record confirms that the source tree and Visual Studio project configuration can be compiled successfully with the documented SDK/toolchain combination.

It does **not** by itself prove:

- successful playback on the T+A SDX 3100 HV;
- renderer-specific MIME/protocol compatibility;
- continuous DSD64/128/256 streaming under load;
- gapless playback;
- compatibility with every SDX firmware revision;
- long-duration hardware stability.

Those items remain runtime/hardware validation tasks and are tracked separately in `HARDWARE_VALIDATION.md` and `RELEASE_CHECKLIST.md`.

## Related build fixes

- `BUILD_FIX_LINKER_0.8_ALPHA3_D.md` — official `libPPUI` solution dependency and related linker cleanup.
- `BUILD_FIX_LINKER_0.8_ALPHA3_E.md` — `pfc` `Debug FB2K` / `Release FB2K` mapping that removes duplicate linker symbols.
- `V142_WTL_FIX.md` — WTL include configuration for MSVC v142.
- `WTL_SETUP.md` — WTL installation and project configuration.


> Note: Alpha 3 F is a subsequent source revision containing UI and roadmap changes. It must be rebuilt on Windows before a new build-validation record is issued.
