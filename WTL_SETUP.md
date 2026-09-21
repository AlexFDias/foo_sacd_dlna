# WTL setup — v142

`foo_sacd_dlna` requires the WTL headers because the component uses headers such as `atlapp.h`. WTL is separate from ATL.

## Relocatable WTL path

The project does **not** require the WTL directory to be named `WTL`.

Place WTL beside the `foobar2000` directory in the SDK tree, with:

```text
<SDK root>\<any WTL folder name>\include\atlapp.h
```

The project automatically searches for `include\atlapp.h`.

Example:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL_renamed\include\atlapp.h
```

No project-file edit is required.

## Explicit path override

Use `WTLIncludeDir`, `WTL_INCLUDE`, or `WTL_ROOT`:

```text
WTLIncludeDir = C:\Libraries\WTL_renamed\include
```

or:

```powershell
$env:WTL_INCLUDE = 'C:\Libraries\WTL_renamed\include'
```

For `WTL_ROOT`, point to the WTL directory itself:

```powershell
$env:WTL_ROOT = 'C:\Libraries\WTL_renamed'
```

## Required headers

At minimum:

```text
include\atlapp.h
include\atlctrls.h
include\atlwin.h
include\atlcrack.h
```

## Visual Studio

Use:

- Visual Studio 2022
- Desktop development with C++
- MSVC v142
- ATL for v142
- Windows SDK
- WTL headers

## Environment check

From a Visual Studio Developer PowerShell:

```powershell
.\tools\check_build_env.ps1
```

To force a path:

```powershell
.\tools\check_build_env.ps1 -WtlInclude 'C:\Libraries\WTL_renamed\include'
```

## Build

```powershell
.\tools\build.ps1 -Configuration Debug -Platform x64
```

With an explicit path:

```powershell
.\tools\build.ps1 -Configuration Debug -Platform x64 -WtlInclude 'C:\Libraries\WTL_renamed\include'
```

See `WTL_RELOCATION.md` for the implementation details and supported layouts.


### Alpha 3 P — WTL discovery fix

A Alpha 3 P corrige a sintaxe MSBuild da descoberta automática do WTL. Não se deve usar `@(WTLMarker)` numa expressão `Condition`; o marcador é agora convertido em propriedade dentro de um `PropertyGroup`. Se existirem várias instalações WTL detectáveis, definir explicitamente `WTLIncludeDir` em `WTL.user.props`.
