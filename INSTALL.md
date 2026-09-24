# INSTALL.md — Installing foo_sacd_dlna

This is for installing an already-built `foo_sacd_dlna.dll` into foobar2000. If you don't have a build yet, see `BUILD.md` first — it covers compiling from source. Everything below assumes you already have (or have just built) two files:

- `foo_sacd_dlna.dll`
- `libFLAC.dll` (Win64, 1.5.x) — needed only for DVD-Audio → FLAC; see step 3.

## 1. Requirements

- **foobar2000, 64-bit**, current version. This component is x64-only.
- **`foo_input_sacd`** — required. Without it the component refuses to start (Console shows `SACD DLNA: Super Audio CD Decoder (foo_input_sacd.dll) is not installed`), even if you only care about DVD-Audio/DSF.
- **`foo_input_dvda`** — required only if you want DVD-Audio → FLAC. DSD/SACD/DSF sharing works without it.
- **`foo_dsd_processor`** — optional, only if you want DSP processing applied before streaming.

Install these the normal way: `File → Preferences → Components → Install...`, or drop the `.fb2k-component` file onto the foobar2000 window.

## 2. Copy the component

1. Close foobar2000.
2. Find your components folder: `File → Preferences → Components`, then look at the "Install" button's target, or open:
   ```text
   %AppData%\foobar2000-v2\user-components\
   ```
   (older foobar2000 installs may use `%AppData%\foobar2000\components\` instead — check which one your Components page actually lists).
3. Create a subfolder there named `foo_sacd_dlna` and copy `foo_sacd_dlna.dll` into it:
   ```text
   %AppData%\foobar2000-v2\user-components\foo_sacd_dlna\foo_sacd_dlna.dll
   ```

## 3. Copy libFLAC.dll (DVD-Audio only, but easy to forget)

If you want DVD-Audio → FLAC, `libFLAC.dll` **must** sit in the exact same folder as `foo_sacd_dlna.dll` — not just somewhere on the build machine, not in a "build output" folder, that exact folder from step 2:

```text
%AppData%\foobar2000-v2\user-components\foo_sacd_dlna\libFLAC.dll
```

This is the single most common installation mistake with this component. Skip it and every DVD-Audio track will fail, one at a time, as an HTTP `503` on the renderer — which most renderers then show as a generic "invalid or unknown format" error, with nothing pointing back at the real cause.

You don't need to guess whether you got this right: as of this release, foobar2000's **Console** (`View → Console`) prints a warning at startup if `libFLAC.dll` is missing, naming the exact path it looked in. If you don't see that warning, you're fine. If you don't need DVD-Audio at all, you can ignore this step and the warning both — DSD/SACD/DSF sharing is unaffected either way.

## 4. Start foobar2000 and enable the component

1. Start foobar2000.
2. `File → Preferences → Tools → SACD DLNA → Settings`.
3. Enable **DLNA**, enable **Share Music Library**.
4. Optionally adjust **Shared formats** (which non-DSD/DVD-A formats to also serve as-is), **Max streams** (default 2), and **Stability Mode** / read-ahead.
5. Click OK / Apply.

## 5. Verify it's working

- `File → Preferences → Tools → SACD DLNA → Status`: should show `BROADCASTING / ACTIVE`. This only confirms discovery is up, not that any audio has actually streamed yet.
- `View → Console`: should show no `libFLAC.dll was not found` warning (unless you deliberately skipped step 3).
- From another device on the same network, open a DLNA/UPnP renderer or control point and look for the server (its name is configurable in Settings; default includes "SACD DLNA").
- Play something. `TRANSMITTING` in the Status panel means audio is actually flowing right now.
- For a scripted check from a second machine on the LAN with Python 3 installed:
  ```bash
  python tools/dlna_smoke_test.py <this-PC-IP> 8192 --get --ssdp
  ```
  (only useful if you also have the source tree; see `BUILD.md`/`EXAMPLES.md`.)

## 6. Firewall

If nothing shows up on other devices, check Windows Firewall allows foobar2000 to receive inbound connections on the configured port (default `8192`) and UDP `1900` (SSDP multicast). See `NETWORK_REQUIREMENTS.md`.

## 7. Updating

To update to a newer build: close foobar2000, replace `foo_sacd_dlna.dll` (and `libFLAC.dll`, if it changed) in the same folder, start foobar2000 again. Existing SACD/DSF/DVD-Audio caches are automatically invalidated if the new build's decoder/cache-format version differs from what generated them — you don't need to clear the cache by hand.

## 8. Uninstalling

`File → Preferences → Components`, select **SACD DLNA Server**, click **Disable** or **Uninstall**. Or, with foobar2000 closed, delete the `foo_sacd_dlna` folder from the components directory in step 2.

## Something not working?

See `HELP.md` for the most common problems (the `libFLAC.dll` one above is by far the most frequent) and `docs/USER_GUIDE.md` for day-to-day usage once it's running.
