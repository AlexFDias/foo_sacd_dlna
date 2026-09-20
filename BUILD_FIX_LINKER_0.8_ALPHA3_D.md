# V0.8 Alpha 3 D — libPPUI linker/build fix

The previous build reached the linker but failed with:

```text
LNK1104: cannot open file '...\\foo_sacd_dlna\\Debug\\libPPUI.lib'
```

## Fix

`libPPUI` is now explicitly included in `foo_sacd_dlna.sln` and is built as a solution project before the component.

Project:

```text
..\\..\\libPPUI\\libPPUI.vcxproj
```

GUID:

```text
{7729EB82-4069-4414-964B-AD399091A03F}
```

This matches the official foobar2000 SDK project.

The component remains:

```text
Platform: x64
PlatformToolset: v142
WTL include:
D:\\SDX_SACD_DSF_DLNA\\SDK-2025-03-07\\WTL\\include
```

The final remaining `inet_addr()` use in the SSDP multicast code was also replaced with `InetPtonA()`.

## Build

In Visual Studio:

```text
Configuration: Debug
Platform: x64
Build → Clean Solution
Build → Rebuild Solution
```

The solution should now show `libPPUI` as an explicit project and build it before `foo_sacd_dlna`.
