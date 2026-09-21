# WTL SDK integration — Alpha 3 S

The plugin project discovers WTL by looking for `include\atlapp.h` under the SDK root, or by using `WTLIncludeDir`, `WTL_INCLUDE`, or `WTL_ROOT`.

The foobar2000 SDK helper projects are separate Visual Studio projects and do not inherit the plugin project's include directories automatically. `tools/apply_sdk_wtl_patch.ps1` patches the two SDK projects that need WTL (`libPPUI` and `foobar2000_sdk_helpers`) so they import the same `WTL.props` through `$(SolutionDir)`.

Run once after changing the WTL folder name:

```powershell
.\tools\apply_sdk_wtl_patch.ps1 -SdkRoot 'D:\SDK-2025-03-07'
```

The script creates `.bak-alpha3-s-wtl` backups before modifying SDK project files.

`tools/install_wtl_support.ps1` now writes `WTL.user.props` with the discovered concrete include directory and patches the two SDK projects that need WTL. This avoids MSBuild item-list/property conversion errors.
