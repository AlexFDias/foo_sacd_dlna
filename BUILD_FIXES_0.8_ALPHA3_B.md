# Build fixes — V0.8 Alpha 3 B

This build addresses compiler errors reported when building with:

- Visual Studio 2022
- MSVC v142 / 14.29.30133
- foobar2000 SDK 2025-03-07
- WTL from `SDK-2025-03-07\WTL\include`
- x64 Debug

## Fixes

- Corrected conversion of `pfc::stringcvt::string_wide_from_utf8_t` to `std::wstring` / `std::filesystem::path` using `get_ptr()`.
- Removed incorrect `.c_str()` calls on the foobar2000 `pfc` wide-string conversion helper.
- Corrected `std::find_if()` result handling: it returns an iterator, not a pointer.
- Preserved the existing v142 and WTL configuration.

## Expected effect

The errors previously reported around lines 121, 1467, 1758, 1777, 1779 and 1799–1812 of `dlna_server.cpp` should be resolved.

The remaining warnings from the SDK about deprecated CRT/Win32 APIs are not fatal build errors.
