# Build Validation — foo_sacd_dlna 0.8 Alpha 3 Q

## Source of change

Alpha 3 P failed with MSBuild MSB4012 because `WTL.props` attempted to assign an item-list transform (`@(WTLMarker->...)`) to a scalar property.

## Q corrections

- Removed item-list-to-property conversion.
- `foo_sacd_dlna.vcxproj` uses the WTL item transform directly in its compiler include path when no explicit `WTLIncludeDir` is set.
- Added an SDK-root `Directory.Build.targets` overlay so `libPPUI` and `foobar2000_sdk_helpers` also receive WTL automatically.
- Added `tools/install_wtl_support.ps1` to install the overlay and detect renamed WTL folders through `include\atlapp.h`.

## Validation status

The change is statically validated in this environment. Windows/MSVC compilation must be rerun by the user.
