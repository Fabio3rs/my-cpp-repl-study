// Example showing safe file inclusion patterns
// Save as: examples/safe_header.h
// Use with: #include "examples/safe_header.h"

#pragma once
#include <iostream>
#include <string>

// ✅ SAFE: Function declarations
void safe_greeting(const std::string& name);
int calculate_sum(int a, int b);

// ✅ SAFE: Class declarations
class SafeCalculator {
public:
    SafeCalculator() = default;
    int add(int a, int b) const { return a + b; }
    int multiply(int a, int b) const { return a * b; }
};

// ✅ SAFE: Inline functions in headers
inline void print_header_message() {
    std::cout << "This is a safe header inclusion!\n";
}

// ❌ AVOID: Global variables in headers that will be included multiple times
// extern std::vector<int> global_data;  // Declare extern instead