# V0.8 Alpha 3 C — linker fix

The previous build failed at link time because the project referenced:

```text
..\..\shared\shared-x64.lib
```

For the official SDK tree used here, the correct location is:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\foobar2000\shared\shared-x64.lib
```

The component now uses:

```text
$(SolutionDir)..\shared\shared-x64.lib
```

The two deprecated `inet_addr()` calls in the SSDP setup were also replaced
with `InetPtonA()`.

Build with:

```text
Debug | x64
PlatformToolset: v142
```

Then:

```text
Build → Clean Solution
Build → Rebuild Solution
```
