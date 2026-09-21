#!/bin/sh
# Compiles the plugin's REAL SacdDlnaServer::browseDidl() and soapActionName() (extracted from
# dlna_server.cpp at run time) against a stub server, then crawls the whole Browse tree like a
# renderer would. Needs g++ (C++17) and python3.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SRC="$HERE/../.."; W=$(mktemp -d)
cp "$HERE"/harness.cpp "$HERE"/crawl.py "$HERE"/soap_test.cpp "$SRC"/library_index.h "$W"/
cd "$W"
python3 - "$SRC" <<'PY'
import re, sys
src = open(sys.argv[1] + '/dlna_server.cpp').read().replace('\r', '')
hdr = open(sys.argv[1] + '/dsf_writer.h').read().replace('\r', '')
a = src.index('std::string SacdDlnaServer::browseDidl('); b = src.index('std::string SacdDlnaServer::browseResponse(')
open('browse_real.inc', 'w').write(src[a:b])
open('consts.inc', 'w').write('\n'.join(re.findall(r'^constexpr const char\* k\w+ = "[^"]*";$', src, re.M)))
open('dsdtrack.inc', 'w').write(re.search(r'struct DsdTrack \{.*?\n\};', hdr, re.S).group(0))
a = src.index('std::string soapActionName('); b = src.index('std::string trimCopy(std::string s) {')
open('soap_real.inc', 'w').write(src[a:b])
PY
echo "== SOAP action recognition"; g++ -std=c++17 -Wall -Wextra -o soap_test soap_test.cpp && ./soap_test
echo "== Browse tree crawl";       g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -o harness harness.cpp && python3 crawl.py
