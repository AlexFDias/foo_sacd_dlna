# V0.8 Alpha 3 — Build Fixes

This build fixes the compilation errors reported against the **foobar2000 SDK 2025-03-07** using the **v142** Visual Studio toolset.

## Important fixes

### DSP preset configuration
The SDK target used by this project exposes legacy-compatible configuration at the selected target level. `cfg_dsp_chain_config` is used instead of declaring `cfg_blob` directly.

### foobar2000 interface implementations
`file_info` and `audio_chunk` are abstract SDK interfaces. The component now uses concrete `file_info_impl` and `audio_chunk_impl_temporary` / `audio_chunk_impl`.

### WTL UI
The status UI includes `helpers/BumpableElem.h`, uses the required protected callback member, and draws through Win32 `DrawTextA` using the `CPaintDC` HDC.

### Main menu
The SDK does not define `mainmenu_groups::tools` in this SDK. The component menu is therefore registered under an SDK-defined main menu group.

### Windows min/max macros
`NOMINMAX` is defined before Windows headers so `std::min` / `std::max` compile correctly.

### Filesystem
MSVC v142 requires an explicit `std::filesystem::path` construction when passing foobar2000 UTF-8→wide conversion objects to filesystem functions.

### Linker
The project now references `shared-x64.lib` from the SDK root's `shared` directory.

## Test

Perform:

```text
Build → Clean Solution
Build → Rebuild Solution
Configuration: Debug
Platform: x64
```

The first milestone is for `foo_sacd_dlna` to compile without errors. Warnings such as SDK deprecation warnings can be handled separately.
