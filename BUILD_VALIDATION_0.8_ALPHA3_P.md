# Build Validation — foo_sacd_dlna 0.8 Alpha 3 P

Status: source/package fix prepared; compile must be revalidated on Windows/MSVC v142.

## Fixed issue
The Alpha 3 O `WTL.props` used `@(WTLMarker)` directly inside an MSBuild `Condition`. MSBuild rejects item-list references in conditions with error C4137-style wording: `Não é permitida uma referência a uma lista de itens ...`.

## Corrected behaviour
`WTL.props` now:
1. honours `WTLIncludeDir`, `WTL_INCLUDE`, and `WTL_ROOT`;
2. discovers a sibling directory containing `include\atlapp.h`;
3. assigns the discovered directory in a PropertyGroup whose condition only references `$(WTLIncludeDir)`;
4. falls back to the conventional `WTL\include` path.

The ARM64/ARM64EC project warnings from `libPPUI` are unrelated to this WTL condition error and can be ignored for the Debug|x64 build.
