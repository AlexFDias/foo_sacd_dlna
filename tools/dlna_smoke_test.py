#!/usr/bin/env python3
"""
dlna_smoke_test.py -- dependency-free UPnP/DLNA smoke test for foo_sacd_dlna.

Referenced from BUILD.md, EXAMPLES.md, HARDWARE_VALIDATION.md and
NETWORK_REQUIREMENTS.md. Uses only the Python standard library (http.client,
xml.etree, socket) so it runs on any machine with Python 3, no install step.

Usage:
    python tools/dlna_smoke_test.py <PC-IP> <port> [--get] [--ssdp]

  <PC-IP>   IP address of the machine running foobar2000 / foo_sacd_dlna.
  <port>    HTTP/DLNA port configured in Preferences (default in the
            component is 8192).
  --get     Also request one media resource with HTTP GET (not just HEAD),
            to confirm actual byte delivery, not just headers.
  --ssdp    Also send an SSDP M-SEARCH multicast and report whether this
            server's own MediaServer response comes back (self-probe).

Exit code is 0 only if every check that was attempted passed.
"""
import argparse
import http.client
import socket
import sys
import xml.etree.ElementTree as ET

SOAP_NS = {
    "s": "http://schemas.xmlsoap.org/soap/envelope/",
    "dc": "http://purl.org/dc/elements/1.1/",
    "upnp": "urn:schemas-upnp-org:metadata-1-0/upnp/",
    "didl": "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/",
}

CDS_URN = "urn:schemas-upnp-org:service:ContentDirectory:1"
CMS_URN = "urn:schemas-upnp-org:service:ConnectionManager:1"


def step(name):
    print(f"\n== {name} ==")


def ok(msg):
    print(f"  PASS  {msg}")


def fail(msg):
    print(f"  FAIL  {msg}")


def http_get(host, port, path, timeout=5.0):
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.request("GET", path)
        resp = conn.getresponse()
        body = resp.read()
        return resp.status, dict(resp.getheaders()), body
    finally:
        conn.close()


def http_head(host, port, path, headers=None, timeout=5.0):
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.request("HEAD", path, headers=headers or {})
        resp = conn.getresponse()
        resp.read()
        return resp.status, dict(resp.getheaders())
    finally:
        conn.close()


def soap_post(host, port, control_path, service_urn, action, args=None, timeout=5.0):
    args = args or {}
    arg_xml = "".join(f"<{k}>{v}</{k}>" for k, v in args.items())
    body = (
        '<?xml version="1.0" encoding="utf-8"?>'
        '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
        's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
        f"<s:Body><u:{action} xmlns:u=\"{service_urn}\">{arg_xml}</u:{action}></s:Body>"
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
        data = resp.read()
        return resp.status, data
    finally:
        conn.close()


def find_first_media_url(browse_didl_xml, host, port):
    """Best-effort: pull the first <res> URL out of a Browse SOAP response."""
    try:
        outer = ET.fromstring(browse_didl_xml)
    except ET.ParseError:
        return None
    result_el = outer.find(".//Result")
    if result_el is None or not result_el.text:
        return None
    try:
        didl = ET.fromstring(result_el.text)
    except ET.ParseError:
        return None
    for res in didl.iter("{urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/}res"):
        if res.text:
            return res.text.strip()
    # tolerate DIDL without namespace prefixes resolved
    for res in didl.iter():
        if res.tag.endswith("}res") or res.tag == "res":
            if res.text:
                return res.text.strip()
    return None


def ssdp_self_probe(port, timeout=3.0):
    msg = (
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        'MAN: "ssdp:discover"\r\n'
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
        "\r\n"
    ).encode("utf-8")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        sock.sendto(msg, ("239.255.255.250", 1900))
        while True:
            try:
                data, addr = sock.recvfrom(4096)
            except socket.timeout:
                return False
            text = data.decode("utf-8", errors="replace")
            if f":{port}" in text and "MediaServer" in text:
                print(f"  self-probe response from {addr[0]}:{addr[1]}")
                return True
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="IP address of the foobar2000/foo_sacd_dlna PC")
    parser.add_argument("port", type=int, help="HTTP/DLNA port (default 8192 in the component)")
    parser.add_argument("--get", action="store_true", help="also GET one media resource, not just HEAD")
    parser.add_argument("--ssdp", action="store_true", help="also run an SSDP self-probe (M-SEARCH)")
    args = parser.parse_args()

    failures = 0

    step("device.xml")
    try:
        status, _, body = http_get(args.host, args.port, "/device.xml")
        if status == 200 and b"MediaServer" in body:
            ok(f"HTTP {status}, MediaServer device description present ({len(body)} bytes)")
        else:
            fail(f"HTTP {status}, unexpected body")
            failures += 1
    except OSError as e:
        fail(f"connection error: {e}")
        failures += 1
        print("\nCannot continue without device.xml; aborting remaining checks.")
        sys.exit(1)

    step("ContentDirectory::Browse (root)")
    browse_body = b""
    try:
        status, browse_body = soap_post(
            args.host, args.port, "/ctl/ContentDirectory", CDS_URN, "Browse",
            {
                "ObjectID": "0", "BrowseFlag": "BrowseDirectChildren",
                "Filter": "*", "StartingIndex": "0", "RequestedCount": "0",
                "SortCriteria": "",
            },
        )
        if status == 200 and b"Result" in browse_body:
            ok(f"HTTP {status}, Browse root returned a Result element")
        else:
            fail(f"HTTP {status}")
            failures += 1
    except OSError as e:
        fail(f"connection error: {e}")
        failures += 1

    step("ContentDirectory::BrowseMetadata (root)")
    try:
        status, body = soap_post(
            args.host, args.port, "/ctl/ContentDirectory", CDS_URN, "Browse",
            {
                "ObjectID": "0", "BrowseFlag": "BrowseMetadata",
                "Filter": "*", "StartingIndex": "0", "RequestedCount": "0",
                "SortCriteria": "",
            },
        )
        if status == 200 and b"Result" in body:
            ok(f"HTTP {status}, BrowseMetadata returned a Result element")
        else:
            fail(f"HTTP {status}")
            failures += 1
    except OSError as e:
        fail(f"connection error: {e}")
        failures += 1

    step("ConnectionManager::GetProtocolInfo")
    try:
        status, body = soap_post(
            args.host, args.port, "/ctl/ConnectionManager", CMS_URN, "GetProtocolInfo"
        )
        if status == 200 and b"Sink" in body:
            ok(f"HTTP {status}, Sink protocol info present")
        else:
            fail(f"HTTP {status}")
            failures += 1
    except OSError as e:
        fail(f"connection error: {e}")
        failures += 1

    step("/status")
    try:
        status, _, body = http_get(args.host, args.port, "/status")
        if status == 200:
            ok(f"HTTP {status}, {len(body)} bytes")
        else:
            fail(f"HTTP {status}")
            failures += 1
    except OSError as e:
        fail(f"connection error: {e}")
        failures += 1

    step("media resource (HEAD, from Browse result)")
    media_url = find_first_media_url(browse_body.decode("utf-8", errors="replace"), args.host, args.port)
    if not media_url:
        print("  SKIP  no <res> URL found in the Browse result (empty library?)")
    else:
        path = media_url
        for prefix in (f"http://{args.host}:{args.port}", f"http://{args.host}"):
            if path.startswith(prefix):
                path = path[len(prefix):]
                break
        try:
            status, headers = http_head(args.host, args.port, path)
            if status == 200:
                ok(f"HTTP {status} on {path} (Content-Length={headers.get('Content-Length', '?')})")
            else:
                fail(f"HTTP {status} on {path}")
                failures += 1
        except OSError as e:
            fail(f"connection error: {e}")
            failures += 1

        if args.get and status == 200:
            step("media resource (GET, byte-range)")
            try:
                s2, h2 = http_head(args.host, args.port, path, headers={"Range": "bytes=0-1023"})
                if s2 in (200, 206):
                    ok(f"HTTP {s2}, Accept-Ranges={h2.get('Accept-Ranges', '?')}")
                else:
                    fail(f"HTTP {s2}")
                    failures += 1
            except OSError as e:
                fail(f"connection error: {e}")
                failures += 1

    if args.ssdp:
        step("SSDP self-probe (M-SEARCH -> 239.255.255.250:1900)")
        if ssdp_self_probe(args.port):
            ok("received a MediaServer response advertising this port")
        else:
            fail("no MediaServer response seen within the timeout (check firewall/UDP 1900)")
            failures += 1

    print(f"\n{'ALL CHECKS PASSED' if failures == 0 else f'{failures} CHECK(S) FAILED'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
