# Shared formats self-test

Validates the plugin's real `parseSharedFormats()` (extracted from `dlna_server.cpp` by `run.sh`, so it always tests the current code): the default `dsf,dff,iso` list, case-insensitivity, leading dots, comma/space/semicolon/tab/newline separators, empty tokens, duplicate collapsing, and that a trailing separator or a lone `.` never ends up matching extension-less files. `./run.sh` needs only g++ (C++17).
