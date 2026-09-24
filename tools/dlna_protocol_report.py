#!/usr/bin/env python3
"""
dlna_protocol_report.py -- dump the full UPnP/DLNA protocol/capability
surface that foo_sacd_dlna advertises, for troubleshooting renderer
compatibility. Complements tools/dlna_smoke_test.py, which only checks
pass/fail; this prints everything so you can compare it against what a
specific renderer's own diagnostics show.

Referenced from EXAMPLES.md. Standard-library only.

Usage:
    python tools/dlna_protocol_report.py <PC-IP> <port>
"""
import argparse
import http.client
import sys
import xml.etree.ElementTree as ET

CMS_URN = "urn:schemas-upnp-org:service:ConnectionManager:1"
CDS_URN = "urn:schemas-upnp-org:service:ContentDirectory:1"


def fetch(host, port, path, timeout=5.0):
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.request("GET", path)
        resp = conn.getresponse()
        return resp.status, dict(resp.getheaders()), resp.read()
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
        return resp.status, resp.read()
    finally:
        conn.close()


def print_protocol_list(title, xml_text):
    print(f"  {title}:")
    if not xml_text:
        print("    (empty)")
        return
    entries = [e.strip() for e in xml_text.split(",") if e.strip()]
    if not entries:
        print("    (empty)")
        return
    for entry in entries:
        print(f"    {entry}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    args = parser.parse_args()

    print(f"foo_sacd_dlna protocol report for {args.host}:{args.port}\n")

    print("== device.xml ==")
    try:
        status, headers, body = fetch(args.host, args.port, "/device.xml")
    except OSError as e:
        print(f"  connection error: {e}")
        sys.exit(1)
    print(f"  HTTP {status}, {len(body)} bytes, Content-Type={headers.get('Content-Type', '?')}")
    try:
        root = ET.fromstring(body)
        for tag in ("friendlyName", "modelName", "modelNumber", "UDN"):
            el = root.find(f".//{{urn:schemas-upnp-org:device-1-0}}{tag}")
            print(f"  {tag}: {el.text.strip() if el is not None and el.text else '?'}")
    except ET.ParseError:
        print("  (could not parse device.xml)")

    print("\n== ConnectionManager::GetProtocolInfo ==")
    try:
        status, body = soap_post(args.host, args.port, "/ctl/ConnectionManager", CMS_URN, "GetProtocolInfo")
        env = ET.fromstring(body)
        source_el = env.find(".//Source")
        sink_el = env.find(".//Sink")
        print_protocol_list("Source", source_el.text if source_el is not None else None)
        print_protocol_list("Sink", sink_el.text if sink_el is not None else None)
    except (OSError, ET.ParseError) as e:
        print(f"  error: {e}")

    print("\n== ContentDirectory root Browse (item count only) ==")
    try:
        status, body = soap_post(
            args.host, args.port, "/ctl/ContentDirectory", CDS_URN, "Browse",
            {
                "ObjectID": "0", "BrowseFlag": "BrowseDirectChildren",
                "Filter": "*", "StartingIndex": "0", "RequestedCount": "0",
                "SortCriteria": "",
            },
        )
        env = ET.fromstring(body)
        for tag in ("NumberReturned", "TotalMatches", "UpdateID"):
            el = env.find(f".//{tag}")
            print(f"  {tag}: {el.text if el is not None else '?'}")
    except (OSError, ET.ParseError) as e:
        print(f"  error: {e}")

    print("\n== /status ==")
    try:
        status, headers, body = fetch(args.host, args.port, "/status")
        print(f"  HTTP {status}, {len(body)} bytes")
    except OSError as e:
        print(f"  error: {e}")

    print(
        "\nCompare the Sink list above against what the target renderer itself\n"
        "reports for GetProtocolInfo (see tools/ta_sdx_probe.py) -- a resource\n"
        "whose MIME/DLNA.ORG_PN is not in the renderer's own Sink list is the\n"
        "most common reason a strict renderer rejects an otherwise-valid file."
    )


if __name__ == "__main__":
    main()
