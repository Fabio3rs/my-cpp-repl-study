#pragma once

#include "simdjson.h"

#ifndef NUSELIBNOTIFY
#include <libnotify/notification.h>
#include <libnotify/notify.h>
#endif

#include <any>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
