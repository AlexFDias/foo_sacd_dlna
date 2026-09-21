#!/bin/sh
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O1 -Wall -Wextra -o /tmp/test_library_index test_library_index.cpp
/tmp/test_library_index
