#!/bin/sh
# Builds the harness from the plugin's real sacd_decode.cpp / dsf_writer.cpp and
# validates the resulting DSF. Needs: g++ (C++17), python3 + numpy, ffmpeg.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SRC="$HERE/../.."; W=$(mktemp -d); cd "$W"
cp "$HERE"/stub.h "$HERE"/harness_main.cpp "$HERE"/gen_dop_test.py "$HERE"/analyze.py .
cp "$SRC"/dsf_writer.h .
printf '#pragma once\n#include "stub.h"\n' > stdafx.h
# std::fstream::open(std::wstring) is an MSVC extension; adapt only this test copy.
sed 's/m_file.open(path,/m_file.open(std::filesystem::path(path),/' "$SRC"/dsf_writer.cpp > dsf_writer.cpp
python3 - "$SRC" <<'PY'
import re, sys
def between(t, s):
    m = re.search(s, t, re.M); e = re.search('^}', t[m.start():], re.M); return t[m.start():m.start()+e.end()]
t = open(sys.argv[1] + '/sacd_decode.cpp').read().replace('\r', '')
open('unpack.cpp', 'w').write('#include "stub.h"\n' + between(t, r'^static const std::array<uint8_t, 256>& bitReverseTable') + '\n' + between(t, r'^bool SacdDecoder::unpackDop') + '\n')
PY
python3 gen_dop_test.py
g++ -std=c++17 -O2 -Wall harness_main.cpp unpack.cpp dsf_writer.cpp -o harness
./harness dop_input.f64 out.dsf
python3 analyze.py
