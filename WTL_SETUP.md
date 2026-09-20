# WTL setup for the v142 build

The foobar2000 SDK helper layer and `foo_sacd_dlna` use WTL headers such as `atlapp.h`.
WTL is separate from ATL.

## Official WTL location used by this project

For the SDK tree used in this project, the WTL headers are expected at:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
```

The following file must exist:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include\atlapp.h
```

Other required WTL headers include:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include\atlctrls.h
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include\atlwin.h
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include\atlcrack.h
```

Keep this WTL tree separate from the source code of `foo_sacd_dlna`. Do not copy individual WTL headers into the component directory.

## Required Visual Studio components

- Visual Studio 2022
- Desktop development with C++
- MSVC v142 / 14.29 x64/x86 build tools
- ATL for v142
- Windows SDK
- WTL headers from the SDK tree above

## Visual Studio configuration

In **Visual Studio → Project Properties → C/C++ → General → Additional Include Directories** the component must see:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
```

Use:

```text
Configuration: All Configurations
Platform: x64
```

The same WTL include directory must be visible to the SDK helper project `foobar2000_sdk_helpers`.

## Using the project property

The component accepts:

```text
WTLIncludeDir = D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
```

or the build script can pass this value to MSBuild.

## PowerShell environment variable (optional)

To keep the path available to tools and scripts:

```powershell
[Environment]::SetEnvironmentVariable(
    'WTL_ROOT',
    'D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL',
    'User'
)
```

Restart Visual Studio after changing the user environment.

## Check the installation

From a Visual Studio Developer PowerShell:

```powershell
.\tools\check_build_env.ps1
```

The checker uses the project's canonical WTL path by default. You can also specify it explicitly:

```powershell
.\tools\check_build_env.ps1 -WtlInclude 'D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include'
```

## Build

Debug:

```powershell
.\tools\build.ps1 -Configuration Debug -Platform x64
```

Release:

```powershell
.\tools\build.ps1 -Configuration Release -Platform x64
```

If you want to pass the path explicitly:

```powershell
.\tools\build.ps1 -Configuration Debug -Platform x64 -WtlInclude 'D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include'
```

Clean and build:

```powershell
.\tools\build.ps1 -Configuration Debug -Platform x64 -Clean
```

## Why `atlapp.h` was missing

The diagnostic:

```text
foobar2000-lite+atl.h(...): fatal error C1083:
'atlapp.h': No such file or directory
```

means the WTL include directory was not visible to the compiler. ATL installation alone does not provide `atlapp.h`.

## Expected build chain

```text
Visual Studio 2022
      │
      ├── MSVC v142
      ├── ATL v142
      ├── Windows SDK
      └── WTL
          │
          └── D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
                    │
                    ▼
           foobar2000 SDK helpers
                    │
                    ▼
              foo_sacd_dlna
```
