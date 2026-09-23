# Release Checklist

Before a public release, verify on a Windows machine with the target foobar2000 build:

Current build validation:
- [x] 0.8 Alpha 3 E Debug x64 build completed without compile/link errors
- [x] Alpha 3 I Debug x64 build confirmed by maintainer; component running
- [x] Alpha 3 J roadmap integration source review completed

Release validation still pending:
- [ ] Visual Studio Release x64 builds cleanly with the pinned SDK
- [ ] `foo_input_sacd` is detected
- [ ] DSF DSD64 plays
- [ ] DSF DSD128 plays
- [ ] DSF DSD256 plays
- [ ] SACD ISO is decoded through the installed `foo_input_sacd`
- [ ] Root / Artist / Album / Track Browse works
- [ ] BrowseMetadata works
- [ ] album art loads
- [ ] HTTP Range works
- [ ] live TX rate is displayed
- [ ] T+A SDX is detected correctly
- [ ] ConnectionManager capability negotiation is recorded
- [ ] no DSD→PCM conversion occurs in the network path
- [ ] cache invalidates after source/decoder changes
- [ ] cancellation leaves no `.partial` cache artifacts
- [ ] Media Library add/remove/modify callbacks refresh the server
- [ ] Windows firewall guidance tested
- [ ] exact SDX firmware recorded in `HARDWARE_VALIDATION.md`
- [ ] 30+ minute DSD256 test completed
- [ ] gapless behaviour recorded as PASS/FAIL, never assumed

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
D:\SDX_SACD_DSF_DLNA\SDK-2025-03-07\WTL\include
```

See `BUILD.md`, `BUILD.md` and `BUILD.md` for the complete configuration.

## Alpha 3 G diagnostic validation note

Alpha 3 G adds new UI, SSDP diagnostics and the network self-probe. These changes are source-level additions and require a fresh Windows **Debug x64 / MSVC v142** rebuild before Alpha 3 G can be marked build-validated. The last build confirmed by the project's Windows log remains Alpha 3 E.


## Alpha 3 H note

Alpha 3 H adds live current-track metadata, source/output technical information and explicit conversion/pipeline state to the existing diagnostics UI. A fresh Windows Debug x64 / MSVC v142 rebuild is required.

## Alpha 3 J functional checks
- [ ] Rebuild Debug x64 with MSVC v142
- [ ] Confirm Status UI shows `LOCAL SSDP READY / WAITING FOR REMOTE PEER` before a renderer contacts the server
- [ ] Confirm a second LAN device produces `CONFIRMED / REMOTE SSDP M-SEARCH` or `CONFIRMED / REMOTE HTTP`
- [ ] Play an album and confirm next-track prefetch reaches `READY`
- [ ] Test SACD ISO cache hit/miss and DSP cache invalidation
- [ ] Validate exact T+A SDX 3100 HV firmware/network path
