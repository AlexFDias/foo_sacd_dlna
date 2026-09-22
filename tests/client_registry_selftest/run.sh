#!/bin/sh
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O1 -Wall -Wextra -pthread -o /tmp/test_client_registry test_client_registry.cpp
/tmp/test_client_registry
