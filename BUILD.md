# BUILD.md — Building foo_sacd_dlna on Windows

This document explains, step by step, how to prepare a Windows PC to **build, test and diagnose** `foo_sacd_dlna`. For the Portuguese version, see `BUILD.pt-PT.md`.

## Build status of this tree

This is **1.0.0**, the first revision where the code, `.vcxproj` and documentation (`BUILD.md`, `CHANGELOG.md`, `docs/`) have been reconciled with each other — see the "1.0.0 — first consolidated release" entry at the top of `CHANGELOG.md` for the concrete list of what was inconsistent and got fixed. It is not a rewrite: it is the same Alpha codebase (full history in `CHANGELOG.md`), now without the contradictions between revisions that had accumulated.

This is **not** a claim of a validated build/hardware pass — there is no Windows/MSVC toolchain here to build this tree. The history of earlier Windows builds (revisions *Alpha 3 E* through *Alpha 3 U*) is in `CHANGELOG.md` and does not prove that 1.0.0 builds; see `docs/VALIDATION_STATUS.md` for what is and isn't confirmed.

On the other hand, this revision **has already been exercised with real field evidence**: diagnostic logs from a test with a real foobar2000 instance, a T+A renderer and VLC showed that the single, consistent, exception-free reason DVD-Audio → FLAC conversion was failing was that `libFLAC.dll` was not sitting next to the installed `foo_sacd_dlna.dll` — not a defect in the FLAC validation code or the encoder. See step 15 below and `FLAC_RUNTIME.md`.

The x64 build uses WTL through `WTL.props`. The location can be supplied via `WTLIncludeDir`, `WTL_INCLUDE` or `WTL_ROOT` — see step 8.


## 1. What you need to build

### Required software

1. **Windows 64-bit**
2. **Visual Studio 2022** or Build Tools 2022
3. **Desktop development with C++** workload
4. **MSVC C++ Build Tools** for x64/x86
5. **Windows SDK**
6. **foobar2000 SDK 2025-03-07**
7. **foobar2000 64-bit** to test the component
8. **foo_input_sacd** to test SACD ISO

The SDK used in this project is **2025-03-07**. The official foobar2000 page states that this version includes project files for Visual Studio 2019/2022. The changelog also states that this version keeps C++17 in certain SDK projects.

- Official SDK: https://www.foobar2000.org/SDK
- SDK changelog: https://www.foobar2000.org/changelog-sdk

### Optional but recommended software

- Git for Windows
- 7-Zip
- Python 3.x — for the DLNA diagnostic scripts
- Wireshark — for analyzing SSDP/HTTP
- Windows Terminal

---

## 2. Recommended hardware for the development machine

No special hardware is needed to **build** the plugin.

| Component | Practical minimum | Recommended |
|---|---|---|
| CPU | 4 threads | 4–8+ physical cores |
| RAM | 8 GB | 16 GB or more |
| Storage | 20 GB free | 40 GB+ on SSD/NVMe |
| GPU | integrated | integrated is enough |
| Network | 100 Mbps | Gigabit Ethernet |

Microsoft currently documents a minimum of 4 GB RAM for Visual Studio 2022, an x64/ARM64 processor, and, for typical professional solutions, 16 GB of RAM recommended; typical installs need 20–50 GB free and Microsoft recommends an SSD.

For this project, **16 GB RAM + SSD/NVMe** is a comfortable configuration, especially once Visual Studio, symbols, SDKs and additional tools are installed.

---

## 3. Install Visual Studio 2022

Download Visual Studio 2022 from Microsoft:

https://visualstudio.microsoft.com/downloads/

The **Community** edition can be used and is sufficient for this project.

In the installer, select:

```text
Workloads
└── Desktop development with C++
```

This workload includes the essential C++ components for Windows, including MSBuild and the C++ build tools. Microsoft's documentation identifies the workload as `Microsoft.VisualStudio.Workload.VCTools`.

### Confirm the components

During installation, confirm at least:

```text
✓ MSVC C++ x64/x86 build tools
✓ MSBuild
✓ Windows SDK
✓ C++ core tools
```

The Windows SDK is normally installed together with the C++ desktop development workload.

---

## 4. Current Visual Studio requirements

For the reference documentation:

https://learn.microsoft.com/en-us/visualstudio/releases/2022/system-requirements

Microsoft currently documents support for 64-bit versions of Windows 11 and Windows Server supported by the relevant edition. Visual Studio 2022 requires .NET Framework 4.8 to run, and the installer uses WebView2 when needed.

For this project, the recommended choice is simple:

```text
Windows 11 x64
Visual Studio 2022
Desktop development with C++
```

---

## 5. Get the foobar2000 SDK

The SDK **should not be bundled automatically in this repository** without checking its distribution terms.

Download it directly from the official page:

https://www.foobar2000.org/SDK

The version currently published on the official page is:

```text
SDK 2025-03-07
```

The official page explicitly states project files for **Visual Studio 2019/2022**.

---

## 6. Recommended folder layout

A simple layout is:

```text
C:\dev\
│
├── SDK-2025-03-07\
│   ├── foobar2000\
│   ├── helpers\
│   ├── pfc\
│   ├── shared\
│   └── ...
│
└── foo_sacd_dlna\
    ├── foo_sacd_dlna.sln
    ├── foo_sacd_dlna.vcxproj
    ├── dlna_server.cpp
    ├── sacd_decode.cpp
    └── ...
```

Keep the two projects at the same level, to simplify the relative references used by the solution.

---

## 7. Open the project

Open:

```text
foo_sacd_dlna.sln
```

in Visual Studio 2022.

Select:

```text
Configuration: Release
Platform: x64
```

For debugging:

```text
Configuration: Debug
Platform: x64
```

Do not use `Win32`/x86 for this project.

---

## 8. Confirm the SDK paths

If you see:

```text
cannot open include file ...
```

open:

```text
Project → Properties
```

and confirm:

```text
C/C++ → Additional Include Directories
```

as well as:

```text
Linker → Additional Library Directories
```

The SDK, `pfc`, `shared` and other component folders must match the SDK you extracted.

Do not assume a fixed path such as `C:\foobar2000-sdk`. Use the actual path of your installation.

---

## 9. Build

In Visual Studio:

```text
Build → Build Solution
```

or:

```text
Ctrl + Shift + B
```

Recommended initial configuration:

```text
Release | x64
```

A successful build should produce the component's DLL in the project's configured output directory.

---

## 10. Build from the command line

Open:

```text
Developer Command Prompt for VS 2022
```

and run:

```powershell
msbuild .\foo_sacd_dlna.sln /m /p:Configuration=Release /p:Platform=x64
```

Clean build:

```powershell
msbuild .\foo_sacd_dlna.sln /t:Clean /p:Configuration=Release /p:Platform=x64
msbuild .\foo_sacd_dlna.sln /t:Build /m /p:Configuration=Release /p:Platform=x64
```

If `msbuild` is not found, you are probably using a regular shell instead of the Visual Studio Developer Command Prompt or Developer PowerShell.

---

## 11. Set up a test foobar2000

It is strongly recommended to use a **separate foobar2000 install/profile** for development.

Do not test an Alpha DLL directly on your main music installation.

The test environment should contain:

```text
foobar2000 x64
foo_input_sacd
foo_sacd_dlna
```

Then restart foobar2000.

---

## 12. Confirm the SACD dependency

Open:

```text
File → Preferences → Tools → SACD DLNA
```

You should see something like:

```text
foo_input_sacd: INSTALLED
```

`foo_input_sacd` is required for the **SACD ISO → DSD** part.

The component was designed to not depend on private functions of the SACD Decoder DLL. The goal is to use foobar2000's public interfaces to request the DSD stream from the installed decoder, allowing the decoder to be updated independently.

---

## 13. First test: DSF

Before testing SACD ISO, use a `.dsf` track that you already know is correct.

This tests:

```text
Music Library
      ↓
foo_sacd_dlna
      ↓
UPnP/DLNA
      ↓
T+A SDX 3100 HV
```

If DSF doesn't work, it isn't worth starting by investigating the SACD Decoder.

---

## 14. Second test: SACD ISO

After DSF works, test:

```text
Album.iso
```

The expected path is:

```text
SACD ISO
   ↓
foo_input_sacd
   ↓
DSD
   ↓
DSF cache
   ↓
HTTP/DLNA
   ↓
SDX 3100 HV
```

The original ISO must not be modified.

---

## 15. Third test: DVD-Audio → FLAC

After DSF and SACD ISO work, test a DVD-Audio track.

This requires three things, all mandatory:

1. **`foo_input_dvda`** installed (the DVD-Audio decoder).
2. The DVD-Audio extension/source included in **Shared formats**.
3. **`libFLAC.dll` (Win64, 1.5.x) copied to the same folder where `foo_sacd_dlna.dll` is installed** in the foobar2000 test profile — usually `%AppData%\foobar2000-v2\user-components\foo_sacd_dlna\` or equivalent. The DLL is at `third_party\libFLAC\Win64\libFLAC.dll` in the source tree; the build already copies it to the project's output folder (`$(OutDir)`), but **that is not foobar2000's components folder** — you have to copy it there yourself, by hand.

This last step is easy to forget because it's a manual copy separate from the build, and forgetting it produces a misleading symptom: every DVD-Audio track fails, one at a time, with HTTP 503 on the renderer (and HTTP 404/503 in VLC), with no obvious indication that a file is missing. As of this revision, if the DLL is not found, foobar2000 shows a line like this right at startup, in the **Console**:

```text
SACD DLNA: libFLAC.dll was not found at "...\libFLAC.dll" -- DVD-Audio to FLAC
conversion will fail for every track until it is copied there (see FLAC_RUNTIME.md).
DSD/SACD sharing is not affected.
```

If you see this line, the test will always fail — fix this first, before investigating anything else.

The expected path, once the DLL is present, is:

```text
DVD-Audio
   ↓
foo_input_dvda
   ↓
PCM 24-bit
   ↓
libFLAC 1.5.x (real encoder, loaded dynamically)
   ↓
.flac cache
   ↓
HTTP/DLNA
   ↓
SDX 3100 HV
```

Confirm the generated cache with the official tools from the FLAC 1.5.0 Win64 distribution:

```powershell
flac.exe -t path\to\the\cache\<id>.flac
metaflac.exe --list path\to\the\cache\<id>.flac
```

`flac -t` should report the file as valid; `metaflac --list` should show the sample rate, channels, bits and total samples expected for the track. See `FLAC_RUNTIME.md` for encoder and cache-validation details.

---

## 16. Test the DLNA server without the SDX

The project includes scripts under:

```text
tools\
```

The smoke-test script can check the MediaServer part without immediately depending on the T+A unit:

```powershell
python .\tools\dlna_smoke_test.py 192.168.1.20 8192
```

Replace `192.168.1.20` with the IP of the PC running foobar2000.

The test checks elements such as:

```text
/device.xml
ContentDirectory::Browse
BrowseMetadata
ConnectionManager::GetProtocolInfo
/status
media resource
```

To also request the audio resource:

```powershell
python .\tools\dlna_smoke_test.py 192.168.1.20 8192 --get
```

---

## 17. Test with the T+A SDX 3100 HV

The recommended network layout is:

```text
PC / foobar2000
      │
   Ethernet
      │
Gigabit Switch
      │
   Ethernet
      │
T+A SDX 3100 HV
```

The interface's status should distinguish:

```text
DLNA: BROADCASTING / ACTIVE
```

from:

```text
Audio stream: ACTIVE / TRANSMITTING
```

The first means the server is available/discoverable.

The second means there is an active HTTP audio transfer.

During playback, look for something like:

```text
T+A SDX: DETECTED / STREAMING
DSD: DSD256
TX: ~22–24 Mbit/s
```

---

## 18. Network requirements

Approximate payload throughput for stereo DSD:

```text
DSD64   ≈ 5.64 Mbit/s
DSD128  ≈ 11.29 Mbit/s
DSD256  ≈ 22.58 Mbit/s
```

TCP/IP, HTTP and UPnP add overhead.

100 Mbps Ethernet is theoretically enough, but for DSD256 the recommended setup is:

```text
Gigabit Ethernet
```

The reason is simple: the goal isn't just to have enough bandwidth, but also to **have headroom** for other devices and temporary congestion.

---

## 19. Windows Firewall

For local DLNA operation, the component normally uses:

```text
UDP 1900
```

for SSDP, and:

```text
TCP 8192
```

for HTTP/media, by default.

If you change the port in Preferences, adjust the firewall rule accordingly.

For the first test, allow foobar2000 on Windows' **Private** network.

Do not expose this DLNA server directly to the Internet.

---

## 20. Test HTTP Range

The renderer may make partial requests:

```http
Range: bytes=...
```

This matters for seeking and certain DLNA playback patterns.

The response should correctly keep:

```text
206 Partial Content
Content-Range
Content-Length
Accept-Ranges: bytes
```

when a valid Range request is received.

---

## 21. Diagnostics with Wireshark

For DLNA problems, Wireshark is extremely useful.

Useful filters:

```text
ssdp
```

```text
http
```

```text
tcp.port == 8192
```

or just the SDX unit:

```text
ip.addr == <SDX-IP>
```

A typical session should look like this:

```text
SDX → SSDP M-SEARCH
PC  → SSDP response
SDX → GET /device.xml
SDX → SOAP Browse
SDX → SOAP BrowseMetadata
SDX → SOAP GetProtocolInfo
SDX → GET /media/<id>.dsf
```

The last request is the point where actual audio transmission starts.

---

## 22. Test DSD256 stability

Initial configuration:

```text
Stability Mode: ON
Pre-buffer: 15 s
```

If the network is quite busy:

```text
Pre-buffer: 20–30 s
```

For very unstable networks:

```text
Pre-buffer: 30–60 s
```

This absorbs temporary variations. It does not fix a connection that permanently stays below the required throughput.

For DSD256, 15 seconds corresponds to approximately **42.3 MB** of stereo DSD payload.

---

## 23. Test the SACD cache

When an ISO is used, the project can create a DSF cache.

The intended behavior is:

```text
First access
ISO → DSD → DSF cache

Subsequent accesses
DSF cache → DLNA
```

The cache must be invalidated when the source or relevant parameters change.

The goal is to avoid repeating SACD conversion unnecessarily while also decoupling decoding from the DLNA client's speed.

---

## 24. Cancellation/concurrency test

During development, test scenarios such as:

1. starting a track;
2. quickly switching to another;
3. cancelling while an ISO is being prepared;
4. opening two tracks/clients concurrently;
5. turning off/restarting the renderer during streaming.

The component must cancel old tasks without leaving incomplete DSF files being served as valid.

---

## 25. Development in Visual Studio

For debugging you can set the foobar2000 executable as the startup application:

```text
Debug → foo_sacd_dlna Properties → Debugging
```

Example:

```text
Executable:
C:\...\foobar2000.exe
```

The path should point to your test installation.

Areas especially useful for breakpoints:

```text
dlna_server.cpp
sacd_decode.cpp
dsf_writer.cpp
preferences.cpp
ui_element.cpp
```

---

## 26. Recommended test order

To reduce the number of variables, test in this order:

```text
1. Build the DLL
2. Load the DLL in foobar2000
3. Preferences
4. foo_input_sacd detection
5. SSDP
6. device.xml
7. Browse
8. BrowseMetadata
9. DSF HTTP GET
10. HTTP Range
11. DSF on the SDX
12. SACD ISO
13. DSF cache
14. libFLAC.dll present (see step 15) + DVD-Audio -> FLAC
15. flac.exe -t / metaflac.exe --list on the generated cache
16. DSD64
17. DSD128
18. DSD256
19. stability/congested network
20. gapless
21. artwork
22. long playback
```

---

## 27. CI / GitHub Actions

The project includes a workflow at `.github/workflows/build.yml` that builds Debug and Release x64 and publishes `foo_sacd_dlna.dll` + `libFLAC.dll` as an artifact.

**Before running it, you must configure two repository variables** (Settings → Secrets and variables → Actions → Variables), because the foobar2000 SDK and WTL are not distributed in this repository (see section 5):

```text
FOOBAR2000_SDK_URL   direct URL to the SDK archive (see https://www.foobar2000.org/SDK)
WTL_URL              direct URL to a WTL archive containing include\atlapp.h
```

Without these variables set, the workflow fails right at the first step with a clear message, instead of failing confusingly later. Confirm both URLs are still valid before relying on this workflow — official download pages change from version to version.

An automated build should produce, at minimum:

```text
BUILD: PASS
ARTIFACT: foo_sacd_dlna
HARDWARE VALIDATION: NOT RUN
```

A green build in GitHub Actions **does not prove T+A compatibility**.

Physical validation has to be done with a real SDX 3100 HV unit, with whatever firmware is actually installed.

---

## 28. Information to record for every release

Always record:

```text
foo_sacd_dlna version
foobar2000 SDK version
Visual Studio version
MSVC toolset version
Windows SDK version
Target architecture
Configuration
Git commit
Windows version
T+A SDX firmware version
```

Example:

```text
foo_sacd_dlna: 1.0.0
foobar2000 SDK: 2025-03-07
Visual Studio: 2022
Configuration: Release
Platform: x64
```

---

## 29. End-user vs. developer requirements

### To develop/build

```text
Windows x64
Visual Studio 2022 / Build Tools
Desktop development with C++
MSVC
Windows SDK
foobar2000 SDK
```

### To run the component

```text
Windows x64
foobar2000 x64
foo_sacd_dlna
foo_input_sacd (required for SACD ISO)
local network
```

The end user **does not need Visual Studio or the SDK** to use an already-built version of the component. See `INSTALL.md`.

---

## 30. Checklist before publishing a release

```text
[ ] Release | x64 builds without errors
[ ] No Debug DLL in the package
[ ] foobar2000 loads the component
[ ] Preferences works
[ ] Help works
[ ] foo_input_sacd is detected
[ ] SSDP works
[ ] Browse works
[ ] BrowseMetadata works
[ ] DSF opens on the renderer
[ ] HTTP Range works
[ ] SACD ISO works
[ ] Cache works
[ ] libFLAC.dll is next to foo_sacd_dlna.dll in the components folder (not just in $(OutDir))
[ ] Console shows no "libFLAC.dll was not found" warning at startup
[ ] DVD-Audio -> FLAC works (foo_input_dvda installed + format shared)
[ ] flac.exe -t / metaflac.exe --list confirm the generated .flac cache
[ ] DSD64 tested
[ ] DSD128 tested
[ ] DSD256 tested
[ ] Artwork tested
[ ] Cancellation tested
[ ] Concurrency tested
[ ] Firewall documented
[ ] Logs checked
[ ] T+A firmware recorded
[ ] T+A hardware tested
```

---

## 31. Official references

### foobar2000

SDK:
https://www.foobar2000.org/SDK

SDK changelog:
https://www.foobar2000.org/changelog-sdk

### Microsoft

Visual Studio 2022 — requirements:
https://learn.microsoft.com/en-us/visualstudio/releases/2022/system-requirements

Desktop development with C++ / workload:
https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools

MSVC Build Tools:
https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc

Windows C++ development:
https://learn.microsoft.com/en-us/cpp/windows/overview-of-windows-programming-in-cpp

## 32. WTL and the v142 toolset

The foobar2000 SDK's helper layer needs the WTL headers, in addition to ATL. This project uses the **v142** toolset:

```xml
<PlatformToolset>v142</PlatformToolset>
```

Do not switch the component to v143 in isolation.

The WTL folder's name is not fixed — the project auto-discovers, via `WTL.props`, a sibling folder containing `include\atlapp.h`:

```text
<SDK root folder>\<WTL folder>\include\atlapp.h
```

You can also specify the path explicitly, with any of these three MSBuild properties: `WTLIncludeDir`, `WTL_INCLUDE` or `WTL_ROOT` (for example in `WTL.user.props`).

### Verify WTL

From a Visual Studio Developer PowerShell:

```powershell
.\tools\check_build_env.ps1
```

or, with an explicit path:

```powershell
.\tools\check_build_env.ps1 -WtlInclude '<SDK root folder>\<WTL folder>\include'
```

### Build with auto-discovered or explicit WTL

```powershell
.\tools\build.ps1 -Configuration Debug -Platform x64
.\tools\build.ps1 -Configuration Release -Platform x64
.\tools\build.ps1 -Configuration Debug -Platform x64 -WtlInclude '<SDK root folder>\<WTL folder>\include'
```

### SDK shared library path

The project resolves the SDK's `shared-x64.lib` as:

```text
$(SolutionDir)..\shared\shared-x64.lib
```

that is, relative to the folder where you placed the SDK (see step 6 — "Recommended folder layout"). It is not a fixed absolute path; adjust your folder layout instead of editing this path.
