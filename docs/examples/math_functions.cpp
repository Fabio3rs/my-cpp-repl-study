// File evaluation demonstration
// Save as: examples/math_functions.cpp
// Use with: #eval examples/math_functions.cpp

#include <cmath>
#include <iostream>

// Mathematical functions
double calculate_circle_area(double radius) {
    return M_PI * radius * radius;
}

double calculate_sphere_volume(double radius) {
    return (4.0 / 3.0) * M_PI * radius * radius * radius;
}

// Template function
template<typename T>
T power_of_two(T value) {
    return value * value;
}

// Test the functions
void test_math_functions() {
    double r = 5.0;
    std::cout << "Circle area (r=" << r << "): " << calculate_circle_area(r) << "\n";
    std::cout << "Sphere volume (r=" << r << "): " << calculate_sphere_volume(r) << "\n";
    std::cout << "Power of 2 (10): " << power_of_two(10) << "\n";
    std::cout << "Power of 2 (2.5): " << power_of_two(2.5) << "\n";
}