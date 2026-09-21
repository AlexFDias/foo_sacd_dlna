# Build validation — Alpha 3 R

## Fix for WTL/MSBuild errors from Alpha 3 Q

The Q build still failed because `@(WTLMarker->...)` was used inside
`AdditionalIncludeDirectories` metadata. MSBuild rejects item-list expressions
when they are embedded in default metadata values.

Alpha 3 R removes **all** `@(WTLMarker)` references from the `.vcxproj` and
performs WTL discovery inside a `Target` that runs immediately before `ClCompile`.
The resolved path is then applied through `ClCompile Update` metadata, which is
valid MSBuild usage.

### Expected behaviour

- No `@(WTLMarker->...)` expression remains in `foo_sacd_dlna.vcxproj`.
- A WTL folder is discovered through `include\atlapp.h` under the SDK root.
- `WTLIncludeDir`, `WTL_INCLUDE`, and `WTL_ROOT` continue to work as explicit overrides.
- If WTL cannot be found, the build stops with a direct diagnostic explaining how to configure it.

## Validation status

Static XML/project validation was performed in this environment.
A real Windows/MSVC v142 build is still required to mark Alpha 3 R compiled successfully.
