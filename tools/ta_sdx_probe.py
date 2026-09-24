#!/usr/bin/env python3
"""
ta_sdx_probe.py -- probe a UPnP/DLNA renderer (e.g. a T+A SDX 3100 HV) directly.

Referenced from PROTOCOL_COMPATIBILITY.md, HARDWARE_VALIDATION.md and
EXAMPLES.md. Standard-library only.

This does NOT talk to foo_sacd_dlna; it talks to the renderer itself, to
capture the exact identity/capability strings the renderer advertises, for
`HARDWARE_VALIDATION.md`.

Usage:
    python tools/ta_sdx_probe.py <SDX-IP> [port]

`port` defaults to 80 (most UPnP renderers serve their device description
there); pass it explicitly if the renderer uses a non-standard port.
"""
import argparse
import http.client
import sys
import xml.etree.ElementTree as ET

DEVICE_NS = "{urn:schemas-upnp-org:device-1-0}"
CMS_URN = "urn:schemas-upnp-org:service:ConnectionManager:1"


def fetch(host, port, path, timeout=5.0):
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.request("GET", path)
        resp = conn.getresponse()
        body = resp.read()
        return resp.status, dict(resp.getheaders()), body
    finally:
        conn.close()


def soap_post(host, port, control_path, service_urn, action, timeout=5.0):
    body = (
        '<?xml version="1.0" encoding="utf-8"?>'
        '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
        's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
        f"<s:Body><u:{action} xmlns:u=\"{service_urn}\"></u:{action}></s:Body>"
        "</s:Envelope>"
    ).encode("utf-8")
    headers = {
        "Content-Type": 'text/xml; charset="utf-8"',
        "SOAPACTION": f'"{service_urn}#{action}"',
        "Content-Length": str(len(body)),
    }
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.request("POST", control_path, body=body, headers=headers)
        resp = conn.getresponse()
        return resp.status, resp.read()
    finally:
        conn.close()


def text_of(el, tag):
    child = el.find(f"{DEVICE_NS}{tag}")
    return child.text.strip() if child is not None and child.text else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="IP address of the renderer")
    parser.add_argument("port", nargs="?", type=int, default=80,
                         help="HTTP port of the renderer's device description (default 80)")
    parser.add_argument("--device-path", default="/description.xml",
                         help="path to the renderer's device description XML "
                              "(varies by device; try /device.xml if the default fails)")
    args = parser.parse_args()

    print(f"== {args.host}:{args.port}{args.device_path} ==")
    try:
        status, _, body = fetch(args.host, args.port, args.device_path)
    except OSError as e:
        print(f"  connection error: {e}")
        sys.exit(1)

    if status != 200:
        print(f"  HTTP {status} -- try a different --device-path (device description "
              f"location is not standardized; common alternatives include "
              f"/description.xml, /device.xml, /upnp/desc.xml)")
        sys.exit(1)

    try:
        root = ET.fromstring(body)
    except ET.ParseError as e:
        print(f"  device description did not parse as XML: {e}")
        sys.exit(1)

    device = root.find(f".//{DEVICE_NS}device")
    if device is None:
        print("  no <device> element found in the description")
        sys.exit(1)

    print(f"  friendlyName     : {text_of(device, 'friendlyName')}")
    print(f"  manufacturer     : {text_of(device, 'manufacturer')}")
    print(f"  modelName        : {text_of(device, 'modelName')}")
    print(f"  modelNumber      : {text_of(device, 'modelNumber')}")
    print(f"  UDN              : {text_of(device, 'UDN')}")

    services = []
    for svc in device.iter(f"{DEVICE_NS}service"):
        service_type = text_of(svc, "serviceType")
        control_url = text_of(svc, "controlURL")
        if service_type:
            services.append((service_type, control_url))
    print(f"  services         : {len(services)}")
    for st, cu in services:
        print(f"    - {st}  (control: {cu})")

    cms = next((s for s in services if "ConnectionManager" in s[0]), None)
    if cms is None:
        print("\n  No ConnectionManager service advertised; cannot query GetProtocolInfo.")
        return

    control_url = cms[1] or "/upnp/control/ConnectionManager1"
    print(f"\n== ConnectionManager::GetProtocolInfo ({control_url}) ==")
    try:
        status, resp_body = soap_post(args.host, args.port, control_url, CMS_URN, "GetProtocolInfo")
    except OSError as e:
        print(f"  connection error: {e}")
        sys.exit(1)
    if status != 200:
        print(f"  HTTP {status}")
        sys.exit(1)
    try:
        env = ET.fromstring(resp_body)
    except ET.ParseError as e:
        print(f"  SOAP response did not parse: {e}")
        sys.exit(1)
    sink_el = env.find(".//Sink")
    if sink_el is not None and sink_el.text:
        print("  Sink:")
        for entry in sink_el.text.split(","):
            entry = entry.strip()
            if entry:
                print(f"    {entry}")
    else:
        print("  No <Sink> element in the response.")

    print(
        "\nRecord these values (model/modelNumber, firmware version from the unit's own\n"
        "menu, and the Sink list above) in HARDWARE_VALIDATION.md's playback matrix."
    )


if __name__ == "__main__":
    main()
