# v142 + WTL configuration

This project uses the same compiler family as the supplied foobar2000 SDK projects:

```xml
<PlatformToolset>v142</PlatformToolset>
```

## Canonical WTL path

The WTL tree used by this project is:

```text
<SDK root>\<WTL folder>\include
```

In particular:

```text
<SDK root>\<WTL folder>\include\atlapp.h
```

The `atlapp.h` error is a WTL include-path problem, not a reason to switch the project from v142 to v143.

## What this project expects

1. Visual Studio 2022 with MSVC v142.
2. ATL for v142.
3. Windows SDK.
4. WTL available at the canonical path above.
5. The WTL `include` directory visible to both `foobar2000_sdk_helpers` and `foo_sacd_dlna`.

## Verify

Run:

```powershell
.\tools\check_build_env.ps1
```

or explicitly:

```powershell
.\tools\check_build_env.ps1 -WtlInclude '<SDK root>\<WTL folder>\include'
```

Then:

```powershell
.\tools\build.ps1 -Configuration Debug -Platform x64
```

The project file accepts `WTLIncludeDir` and defaults to the canonical SDK-relative WTL directory when no override is supplied.


## Alpha 3 O relocation change

The previous documentation used a fixed folder name for WTL. Alpha 3 O removes that requirement. `WTL.props` auto-discovers a sibling WTL directory by checking for `include\atlapp.h`; explicit `WTLIncludeDir`, `WTL_INCLUDE`, and `WTL_ROOT` overrides remain supported.


### Alpha 3 P — WTL discovery fix

A Alpha 3 P corrige a sintaxe MSBuild da descoberta automática do WTL. Não se deve usar `@(WTLMarker)` numa expressão `Condition`; o marcador é agora convertido em propriedade dentro de um `PropertyGroup`. Se existirem várias instalações WTL detectáveis, definir explicitamente `WTLIncludeDir` em `WTL.user.props`.
