# WTL relocation — Alpha 3 R

The project no longer depends on the folder being named `WTL` and does not use
MSBuild item-list transforms inside scalar/default metadata.

## Automatic discovery

The component searches under the SDK root for a sibling directory containing:

`include\atlapp.h`

For example, all of these are valid folder names:

- `WTL`
- `WTL_2025`
- `WTL_New`
- `MyWTL`

## Explicit override

Set one of these MSBuild properties when automatic discovery is not desired:

`WTLIncludeDir=C:\path\to\wtl\include`

`WTL_INCLUDE=C:\path\to\wtl\include`

`WTL_ROOT=C:\path\to\wtl`

## Why Alpha 3 R changed the implementation

MSBuild does not allow an item-list transform such as `@(WTLMarker->...)` to be
embedded in `AdditionalIncludeDirectories` default metadata. Alpha 3 R performs
that discovery in a build target and updates `ClCompile` items afterwards.
