#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <helpers/foobar2000+atl.h>
#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/dsp_manager.h>
#include <foobar2000/SDK/componentversion.h>
#include <foobar2000/SDK/preferences_page.h>
#include <foobar2000/SDK/library_manager.h>
#include <foobar2000/SDK/library_callbacks.h>
#include <helpers/input_helpers.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <initializer_list>
#include <set>
#include <memory>
#include <condition_variable>
#include <map>
#include <numeric>

#pragma comment(lib, "Ws2_32.lib")
