#pragma once
#include <exception>
#include <utility>

namespace backtraced_exceptions {

auto get_backtrace_for(const std::exception &e)
    -> std::pair<void *const *, int>;

void print_backtrace(void *const *btrace, int size, bool print_lines = true);

} // namespace backtraced_exceptions
