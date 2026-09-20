# V0.8 Alpha 3 F — View / SACD DLNA UI fix

The SACD DLNA status window was previously exposed only through the generic UI-element popup mechanism. Alpha 3 F adds an explicit `View → SACD DLNA → Open SACD DLNA Status` main-menu command and explicit popup dimensions/title.

`View → SACD DLNA → Configure DSD Processor...` now opens the installed DSD Processor configuration directly.

The old `SACD DLNA Status` UI element remains available through foobar2000's UI element selection system.

Build validation: Alpha 3 E is the last confirmed Windows build. Alpha 3 F requires a fresh Windows rebuild after these source changes.


## Alpha 3 G follow-up

The previous menu fix remains in place. Alpha 3 G extends the same status surface into a diagnostic panel with live read-ahead, network and SSDP validation information.
