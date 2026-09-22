#!/bin/sh
# Extracts the plugin's real parseSharedFormats() and lowerCopy() and tests them. Needs g++ (C++17), python3.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SRC="$HERE/../.."; W=$(mktemp -d)
cp "$HERE"/test_shared_formats.cpp "$W"/
python3 - "$SRC" "$W" <<'PY'
import sys
src = open(sys.argv[1] + '/dlna_server.cpp').read().replace('\r', '')
def fn(start):
    a = src.index(start); b = src.index('\n}\n', a) + 3; return src[a:b]
open(sys.argv[2] + '/extracted.inc', 'w').write('#include <cctype>\n' + fn('std::string lowerCopy(std::string s) {') + '\n' + fn('static std::unordered_set<std::string> parseSharedFormats('))
PY
cd "$W" && g++ -std=c++17 -Wall -Wextra -o test_shared_formats test_shared_formats.cpp && ./test_shared_formats
