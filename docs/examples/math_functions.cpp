// File evaluation demonstration
// Save as: examples/math_functions.cpp
// Use with: #eval examples/math_functions.cpp (RECOMMENDED)
// AVOID:    #include "examples/math_functions.cpp" (DANGEROUS - double-free risk)

#include <cmath>
#include <iostream>
#include <vector>

// ⚠️ GLOBAL VARIABLES: This is why #eval is safer than #include
std::vector<double> calculation_history;
static int calculation_count = 0;

// Mathematical functions that modify global state
double calculate_circle_area(double radius) {
    calculation_count++;
    double result = M_PI * radius * radius;
    calculation_history.push_back(result);
    return result;
}

double calculate_sphere_volume(double radius) {
    calculation_count++;
    double result = (4.0 / 3.0) * M_PI * radius * radius * radius;
    calculation_history.push_back(result);
    return result;
}

// Template function
template<typename T>
T power_of_two(T value) {
    return value * value;
}

// Function that shows global state
void print_calculation_stats() {
    std::cout << "Calculations performed: " << calculation_count << std::endl;
    std::cout << "History size: " << calculation_history.size() << std::endl;
}

// Test the functions
void test_math_functions() {
    double r = 5.0;
    std::cout << "Circle area (r=" << r << "): " << calculate_circle_area(r) << "\n";
    std::cout << "Sphere volume (r=" << r << "): " << calculate_sphere_volume(r) << "\n";
    std::cout << "Power of 2 (10): " << power_of_two(10) << "\n";
    std::cout << "Power of 2 (2.5): " << power_of_two(2.5) << "\n";
    print_calculation_stats();
}

// Note: When using #eval, you can reference these globals safely:
// >>> #eval examples/math_functions.cpp
// >>> calculation_count  // Works - extern declaration handled by REPL
// >>> test_math_functions()  // Safe access to globals