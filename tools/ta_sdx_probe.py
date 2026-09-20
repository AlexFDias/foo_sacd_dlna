#!/usr/bin/env python3
"""Probe a T+A UPnP renderer and print its device description / ConnectionManager.

Usage:
    python tools/ta_sdx_probe.py 192.168.1.50 80 /path/to/device.xml

The normal invocation is:
    python tools/ta_sdx_probe.py 192.168.1.50

The script downloads common UPnP device-description paths and, when found,
prints manufacturer/model/serial information and ConnectionManager protocol info.
It does not change the T+A device.
"""
import sys, http.client, re
from urllib.parse import urlparse, urljoin

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 80
PATH = sys.argv[3] if len(sys.argv) > 3 else "/device.xml"

def request(host, port, method, path, body=None, headers=None):
    c = http.client.HTTPConnection(host, port, timeout=5)
    c.request(method, path, body or "", headers or {})
    r = c.getresponse()
    return r.status, dict(r.getheaders()), r.read().decode("utf-8", "replace")

def tag(xml, name):
    m = re.search(r"<([\w-]+:)?" + re.escape(name) + r"[^>]*>(.*?)</([\w-]+:)?" + re.escape(name) + r">", xml, re.I|re.S)
    return re.sub(r"<[^>]+>", "", m.group(2)).strip() if m else ""

candidates = [PATH, "/description.xml", "/DeviceDescription.xml"]
xml = ""
for p in candidates:
    try:
        status, headers, body = request(HOST, PORT, "GET", p)
        if status == 200 and "<device" in body.lower():
            xml = body
            PATH = p
            print(f"Device description: http://{HOST}:{PORT}{PATH}")
            break
    except OSError as e:
        print("Connection error:", e)
        raise SystemExit(2)
if not xml:
    print("No UPnP device description found at the candidate paths.")
    raise SystemExit(1)

for n in ("friendlyName", "manufacturer", "modelName", "modelNumber", "serialNumber", "UDN"):
    print(f"{n}: {tag(xml, n)}")

services = []
for b in re.findall(r"<service\b.*?</service>", xml, re.I|re.S):
    st = tag(b, "serviceType")
    cu = tag(b, "controlURL")
    if st:
        services.append((st, cu))
print("Services:")
for st, cu in services:
    print("  ", st, cu)

for st, cu in services:
    if "connectionmanager" not in st.lower():
        continue
    path = urljoin(f"http://{HOST}:{PORT}{PATH}", cu).split(f"http://{HOST}:{PORT}",1)[-1]
    service = st
    body = ('<?xml version="1.0" encoding="utf-8"?>'
            '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
            's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
            f'<s:Body><u:GetProtocolInfo xmlns:u="{service}"></u:GetProtocolInfo></s:Body></s:Envelope>')
    status, headers, reply = request(HOST, PORT, "POST", path, body,
        {"Content-Type": 'text/xml; charset="utf-8"', "SOAPACTION": f'"{service}#GetProtocolInfo"'})
    print("GetProtocolInfo HTTP:", status)
    print("Sink:", tag(reply, "Sink"))
    print("Source:", tag(reply, "Source"))
