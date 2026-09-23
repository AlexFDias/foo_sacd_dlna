# foo_sacd_dlna — Developer Guide

## Source baseline

- Version file: `VERSION`
- Current tree value: `0.8-alpha3-u-dvda-flac`
- SDK: foobar2000 SDK 2025-03-07
- Windows target: x64
- Documented compiler family: MSVC v142
- UI framework used by the component: WTL/ATL as supplied/configured for the SDK tree

## Build

Open `foo_sacd_dlna.sln` in Visual Studio 2022. Configure the SDK and WTL according to `BUILD.md`, `WTL_SETUP.md`, `WTL_RELOCATION.md` and `WTL_SDK_INTEGRATION.md`.

A source edit is not considered build-validated until a real Windows/MSVC build is recorded in the corresponding validation document.

## Tests

The repository includes focused self-tests under `tests/`:

- `client_registry_selftest`
- `shared_formats_selftest`
- `dsf_dop_selftest`
- `library_index_selftest`
- `browse_tree_selftest`

Run the shell wrappers with `sh tests/<name>/run.sh` when the required tools are available.

## UI rules

Preferences pages must remain native `preferences_page_instance` children. Do not resize or manipulate the foobar2000 Preferences host window to compensate for control layout.

The current child dialog resources are 450×400 dialog units for Status, Settings and Maintenance.

## Concurrency

Do not replace the atomic stream admission control with a check-then-increment counter. The limit must be enforced at the point where a stream is actually admitted for transmission, and the slot must be released on every exit path.

Client accounting and stream accounting are separate concepts.

## Caching

Any generated audio cache must have a deterministic cache key and, where applicable, a manifest containing the decoder/processing state required to establish validity. Temporary files must not become visible as complete media.

## Documentation rule

When changing behavior, update:

1. `CHANGELOG.md` for the historical change record;
2. the relevant consolidated document in `docs/`;
3. `PREFERENCES_FIELDS.md` if a Preferences field changes;
4. validation documentation if the change affects build/runtime claims;
5. hardware validation documentation when renderer-specific behavior changes.
