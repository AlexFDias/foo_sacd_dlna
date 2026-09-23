# Network Requirements

## Recommended setup

Use wired Gigabit Ethernet whenever possible:

```text
Windows PC / foobar2000
          │
       1 GbE
          │
       Switch
          │
       1 GbE
          │
T+A SDX 3100 HV
```

Native stereo DSD payload rates are approximately:

- DSD64: 5.64 Mbit/s
- DSD128: 11.29 Mbit/s
- DSD256: 22.58 Mbit/s

100 Mbps Ethernet can carry these rates in theory, but Gigabit Ethernet is recommended for headroom and network congestion.

The SDX 3100 HV provides 10/100/1000 Base-T Ethernet and Wi-Fi. T+A documents DFF/DSF and DSD64/DSD128/DSD256 for the Streaming Client.

## Stability Mode

Use Stability Mode on busy networks. The default 15-second read-ahead provides approximately:

- DSD64: 10.6 MB
- DSD128: 21.2 MB
- DSD256: 42.3 MB

These are approximate raw stereo DSD payload values.

Stability Mode is designed for short interruptions and throughput fluctuations. It cannot overcome a sustained link slower than the required bitrate.

## Wi-Fi

5 GHz Wi-Fi can work, but wired Ethernet is preferred for predictable latency and reduced susceptibility to interference.

2.4 GHz Wi-Fi should not be the preferred transport for DSD256.

## Firewall

Allow the foobar2000 application/component to accept:

- TCP: the configured HTTP/DLNA port (default 8192)
- UDP: SSDP multicast 239.255.255.250:1900

The PC and T+A should normally be on the same LAN/VLAN for SSDP discovery.

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
<SDK root>\<WTL folder>\include
```

See `BUILD.md`, `BUILD.md` and `BUILD.md` for the complete configuration.

## Live UPnP / DLNA network validation

The status UI distinguishes three different conditions:

```text
SSDP NOTIFY sent      = the server successfully sent discovery announcements
HTTP self-test        = this PC can reach its own UPnP XML endpoints
SSDP self-probe       = this PC sent M-SEARCH to 239.255.255.250:1900 and received its own MediaServer response
PRESENCE CONFIRMED    = actual traffic from another LAN peer has been observed
```

The audio stream itself is **unicast HTTP/TCP**. SSDP multicast is discovery/announcement only; it does not carry DSD audio.

Use **View → SACD DLNA → Open SACD DLNA Status** for the live panel. Right-clicking that panel runs the network probe. The Preferences page also provides **Run Network Probe**.

For packet-level troubleshooting, capture UDP `239.255.255.250:1900` and TCP traffic to the configured HTTP port in Wireshark.
### Network probe

Use **Run Network Probe** to validate the local UPnP HTTP endpoints and perform an SSDP MediaServer multicast self-probe. The status UI separately reports `PRESENCE CONFIRMED` only after actual non-local HTTP/SSDP traffic is observed.


## Alpha 3 H — live audio information

The SACD DLNA Status panel now shows the active music title/artist/album, source and output formats, source/output file size, DSD/PCM sample rate, channels and bit depth, effective network speed in x-realtime, and the active processing pipeline. The conversion label distinguishes native DSD (`NO CONVERSION`), SACD ISO decoding/cache, and DSD Processor output (`DSP OUTPUT / CACHED` or `DSP CONVERTING`).

## Alpha 3 H — live audio diagnostics

The diagnostic UI includes current music metadata, source/output format and size, audio resolution, measured TX speed, x-realtime transmission rate, and explicit native/DSP/SACD conversion states.

### View → SACD DLNA menu hardening — Alpha 3 K

The complete View → SACD DLNA command set was reviewed. Library sharing is now a true toggle, the preferences command opens the dedicated SACD DLNA page directly, and refresh/clear-library/clear-cache actions are available from the same menu. DSD Processor toggling now only re-indexes an already shared/running DLNA library.
## Alpha 3 N — Windows discovery hardening

Improved Windows UPnP/DLNA discovery compatibility: LAN-interface selection for the advertised LOCATION, DLNA device namespace/description, SSDP service announcements and service-type M-SEARCH responses. Added explicit advertised LOCATION diagnostics. Windows Explorer discovery remains dependent on the Windows SSDP/Function Discovery stack and firewall configuration.

