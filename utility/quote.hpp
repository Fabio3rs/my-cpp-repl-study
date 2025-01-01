#pragma once

#include <string>

namespace utility {
auto quote(std::string str, char quoteChar = '"', char escapeChar = '\\')
    -> std::string;
} // namespace utility
