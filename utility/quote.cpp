#include "quote.hpp"
#include <algorithm>
#include <string>

namespace utility {

std::string quote(std::string str, char quoteChar, char escapeChar) {
    auto count = std::count_if(str.begin(), str.end(), [=](char c) {
        return c == quoteChar || c == escapeChar;
    });

    str.reserve(str.size() + count + 2);

    for (auto it = str.begin(); it != str.end(); ++it) {
        if (*it == '"' || *it == '\\') {
            it = str.insert(it, '\\');
            ++it;
        }
    }

    str.insert(0, 1, '"');
    str.push_back('"');

    return str;
}

} // namespace utility
