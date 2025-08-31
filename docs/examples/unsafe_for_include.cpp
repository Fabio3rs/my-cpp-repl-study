// Example showing what should be used with #eval instead of #include
// Save as: examples/unsafe_for_include.cpp
// Use with: #eval examples/unsafe_for_include.cpp

#include <vector>
#include <iostream>

// ❌ GLOBAL VARIABLES: This file should NOT be used with #include
// Use #eval instead to prevent double-free issues
std::vector<double> calculation_cache = {1.0, 2.0, 3.0};
static int operation_count = 0;

// Functions that modify global state
double cached_multiply(double a, double b) {
    operation_count++;
    double result = a * b;
    calculation_cache.push_back(result);
    return result;
}

void print_cache_stats() {
    std::cout << "Operations performed: " << operation_count << std::endl;
    std::cout << "Cache size: " << calculation_cache.size() << std::endl;
}

// Example usage function
void demonstrate_unsafe_globals() {
    std::cout << "=== Demonstration of why this file needs #eval ===\n";
    
    double result1 = cached_multiply(3.14, 2.0);
    double result2 = cached_multiply(5.0, 7.0);
    
    std::cout << "Results: " << result1 << ", " << result2 << std::endl;
    print_cache_stats();
}

// Note: When using #eval, the REPL will handle extern declarations properly
// and you can reference these globals safely from subsequent code