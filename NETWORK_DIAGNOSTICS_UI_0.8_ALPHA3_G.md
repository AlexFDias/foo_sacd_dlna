# v0.8 Alpha 3 G — Network diagnostics UI

## What is validated

The UI separates four conditions that are easy to confuse:

1. **SSDP NOTIFY sent** — the component successfully sent an SSDP announcement to `239.255.255.250:1900`.
2. **HTTP self-test OK** — the local HTTP server answered `device.xml`, `ContentDirectory.xml` and `ConnectionManager.xml`.
3. **SSDP self-probe OK** — a local M-SEARCH for `MediaServer:1` received this server's own response.
4. **PRESENCE CONFIRMED** — a non-local network peer has actually contacted the server by HTTP or SSDP.

## What is not claimed

A successful local self-probe does not prove end-to-end discovery through every VLAN, switch, AP, firewall or renderer firmware. A T+A SDX playback test remains the final hardware validation.

## Debug option

`Enable Debug Diagnostics` enables the extended state and diagnostic logging. It does not change audio format, DSD rate or the renderer protocol.

## Buffer UI

The buffer bar represents the server-side Stability Mode read-ahead reserve. It is intentionally labelled as such and must not be interpreted as the SDX renderer's playback FIFO.

## Alpha 3 J — remote network visibility

The live status panel now distinguishes local protocol validation from actual remote visibility.

- `LOCAL SSDP READY / WAITING FOR REMOTE PEER`: the server has joined the SSDP multicast group but no non-local peer has contacted it yet.
- `CONFIRMED / REMOTE SSDP M-SEARCH`: a non-local device sent an M-SEARCH to the MediaServer endpoint.
- `CONFIRMED / REMOTE HTTP`: a non-local device opened a TCP/HTTP request to the server.

This is the strongest in-component evidence available without requiring a second machine to participate in a test. A local self-probe is intentionally not counted as remote visibility.

## External visibility test

For end-to-end proof that another machine can discover this MediaServer, run the dependency-free smoke test from a second device on the same LAN:

```text
python tools/dlna_smoke_test.py <PC-IP> 8192 --ssdp
```

A successful response confirms that the second device can send SSDP M-SEARCH and receive this server's MediaServer response. The in-component Status panel reports the same class of evidence once a real remote peer sends M-SEARCH or HTTP traffic.
