#!/usr/bin/env python3
"""Produce a compact UPnP/DLNA protocol report for foo_sacd_dlna."""
import re, sys, http.client
HOST = sys.argv[1] if len(sys.argv)>1 else '127.0.0.1'
PORT = int(sys.argv[2]) if len(sys.argv)>2 else 8192

def req(method,path,body='',headers=None):
    c=http.client.HTTPConnection(HOST,PORT,timeout=5); c.request(method,path,body,headers or {})
    r=c.getresponse(); b=r.read(); h=dict(r.getheaders()); c.close(); return r.status,h,b

st,h,b=req('GET','/device.xml'); print('device.xml:',st,h.get('Content-Type')); print('MediaServer:', b'MediaServer' in b)
st,h,b=req('GET','/ContentDirectory.xml'); print('ContentDirectory SCPD:',st,'Browse=',b'<name>Browse</name>' in b,'BrowseMetadata supported by Browse action=',b'BrowseFlag' in b)
st,h,b=req('GET','/ConnectionManager.xml'); print('ConnectionManager SCPD:',st,'GetProtocolInfo=',b'GetProtocolInfo' in b)
service='urn:schemas-upnp-org:service:ConnectionManager:1'
env='<?xml version="1.0" encoding="utf-8"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:GetProtocolInfo xmlns:u="'+service+'"></u:GetProtocolInfo></s:Body></s:Envelope>'
st,h,b=req('POST','/ctl/ConnectionManager',env,{'Content-Type':'text/xml; charset="utf-8"','SOAPACTION':'"'+service+'#GetProtocolInfo"'})
print('GetProtocolInfo:',st)
text=b.decode('utf-8','replace')
m=re.search(r'<Source>(.*?)</Source>',text,re.S|re.I)
print('SourceProtocolInfo:',m.group(1) if m else '(missing)')
st,h,b=req('GET','/status'); print('status:',st); print(b.decode('utf-8','replace')[:5000])
print('Tip: run dlna_smoke_test.py with --ssdp to validate MediaServer multicast discovery on the LAN.')
