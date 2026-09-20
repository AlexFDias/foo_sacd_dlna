#!/usr/bin/env python3
"""Dependency-free HTTP/SOAP smoke test for foo_sacd_dlna.

Usage:
  python tools/dlna_smoke_test.py 192.168.1.10 8192
  python tools/dlna_smoke_test.py 192.168.1.10 8192 --get
  python tools/dlna_smoke_test.py 192.168.1.10 8192 --ssdp

Run the --ssdp form from a second LAN machine to verify remote MediaServer visibility.
"""
import http.client
import re
import socket
import sys
import time
from urllib.parse import urlsplit, unquote

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8192
DO_GET = "--get" in sys.argv
DO_SSDP = "--ssdp" in sys.argv


def http(method, path, body=None, headers=None):
    c = http.client.HTTPConnection(HOST, PORT, timeout=5)
    c.request(method, path, body=body, headers=headers or {})
    r = c.getresponse()
    b = r.read()
    h = dict(r.getheaders())
    c.close()
    return r.status, h, b


def soap_service(service, action, body, path):
    env = ('<?xml version="1.0" encoding="utf-8"?>'
           '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
           's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
           '<s:Body><u:' + action + ' xmlns:u="' + service + '">' + body + '</u:' + action + '>'
           '</s:Body></s:Envelope>')
    return http('POST', path, env, {
        'Content-Type': 'text/xml; charset="utf-8"',
        'SOAPACTION': f'"{service}#{action}"',
    })


def soap(action, body):
    service = "urn:schemas-upnp-org:service:ContentDirectory:1"
    env = ('<?xml version="1.0" encoding="utf-8"?>'
           '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
           's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
           '<s:Body><u:' + action + ' xmlns:u="' + service + '">' + body + '</u:' + action + '>'
           '</s:Body></s:Envelope>')
    return http('POST', '/ctl/ContentDirectory', env, {
        'Content-Type': 'text/xml; charset="utf-8"',
        'SOAPACTION': f'"{service}#{action}"',
    })



def ssdp_probe():
    group = ("239.255.255.250", 1900)
    msg = ("M-SEARCH * HTTP/1.1\r\n"
           "HOST: 239.255.255.250:1900\r\n"
           "MAN: \"ssdp:discover\"\r\n"
           "MX: 1\r\n"
           "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n\r\n").encode("ascii")
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    s.settimeout(2.0)
    s.bind(("", 0))
    s.sendto(msg, group)
    deadline = time.time() + 2.2
    seen_self = False
    while time.time() < deadline:
        try:
            data, addr = s.recvfrom(8192)
        except socket.timeout:
            break
        text = data.decode("utf-8", "replace")
        if not text.startswith("HTTP/1.1 200"):
            continue
        loc = re.search(r"^LOCATION:\s*(.+)$", text, re.I | re.M)
        usn = re.search(r"^USN:\s*(.+)$", text, re.I | re.M)
        location = loc.group(1).strip() if loc else ""
        usn_value = usn.group(1).strip() if usn else ""
        if f"http://{HOST}:{PORT}/" in location:
            seen_self = True
            print(f"[OK] SSDP MediaServer response from {addr[0]}: {location or usn_value}")
            break
    s.close()
    if not seen_self:
        raise RuntimeError("No SSDP MediaServer self-response observed; check Windows Firewall/VLAN/multicast and the configured HTTP port")



def get_protocol_info():
    service = "urn:schemas-upnp-org:service:ConnectionManager:1"
    return soap_service(service, "GetProtocolInfo", "", "/ctl/ConnectionManager")

def browse(object_id, flag='BrowseDirectChildren'):
    args = (f'<ObjectID>{object_id}</ObjectID><BrowseFlag>{flag}</BrowseFlag>'
            '<Filter>*</Filter><StartingIndex>0</StartingIndex><RequestedCount>100</RequestedCount>'
            '<SortCriteria></SortCriteria>')
    st, _, body = soap('Browse', args)
    if st >= 300:
        raise RuntimeError(f'Browse {object_id}: HTTP {st}')
    m = re.search(r'<Result>(.*?)</Result>', body.decode('utf-8', 'replace'), re.S)
    if not m:
        raise RuntimeError(f'Browse {object_id}: no Result')
    result = m.group(1)
    result = (result.replace('&lt;', '<').replace('&gt;', '>')
                    .replace('&quot;', '"').replace('&amp;', '&').replace('&apos;', "'"))
    return result


def containers(didl):
    return re.findall(r'<container\b[^>]*\bid="([^"]+)"[^>]*>.*?<dc:title>(.*?)</dc:title>.*?</container>', didl, re.S)


def items(didl):
    return re.findall(r'<item\b[^>]*\bid="([^"]+)"[^>]*>.*?<dc:title>(.*?)</dc:title>.*?<res\b[^>]*>(http://[^<]+)</res>', didl, re.S)

print(f'foo_sacd_dlna smoke test: http://{HOST}:{PORT}')
st, _, device = http('GET', '/device.xml')
assert st == 200 and b'MediaServer' in device
print('[OK] device.xml')

root = browse('0')
print(f'[OK] root: {len(containers(root))} container(s)')
artists = [x for x in containers(root) if x[0] == 'artists']
assert artists
artist_list = browse(artists[0][0])
print(f'[OK] artists: {len(containers(artist_list))}')

if containers(artist_list):
    alb_list = browse(containers(artist_list)[0][0])
    print(f'[OK] first artist albums: {len(containers(alb_list))}')
    if containers(alb_list):
        track_list = browse(containers(alb_list)[0][0])
        tracks = items(track_list)
        print(f'[OK] first album tracks: {len(tracks)}')
        if tracks:
            tid, title, url = tracks[0]
            print(f'[OK] first track: {title}')
            md = browse(tid, 'BrowseMetadata')
            assert tid in md
            print('[OK] BrowseMetadata for track')
            u = urlsplit(url)
            path = unquote(u.path)
            if DO_GET:
                st, h, b = http('GET', path)
                print(f'[OK] GET media: HTTP {st}, {len(b)} bytes')
            else:
                st, h, _ = http('HEAD', path)
                assert st == 200, f'HEAD media returned HTTP {st}'
                assert h.get('Content-Type', '').lower() in ('audio/x-dsf', 'audio/dsf'), 'unexpected DSF content type'
                print(f'[OK] HEAD media: HTTP {st}, type={h.get("Content-Type", "")}, length={h.get("Content-Length", "")}')
                st2, h2, b2 = http('GET', path, headers={'Range': 'bytes=0-63'})
                assert st2 == 206, f'Range request returned HTTP {st2}'
                assert len(b2) == 64, f'Range request returned {len(b2)} bytes'
                assert h2.get('Content-Range', '').startswith('bytes 0-63/'), 'missing/invalid Content-Range'
                print(f'[OK] Range GET: HTTP {st2}, bytes={len(b2)}, content-range={h2.get("Content-Range", "")}')

st, _, cm = get_protocol_info()
assert st == 200 and b'GetProtocolInfoResponse' in cm
print('[OK] ConnectionManager GetProtocolInfo')

if DO_SSDP:
    ssdp_probe()

print('[DONE] smoke test passed')
