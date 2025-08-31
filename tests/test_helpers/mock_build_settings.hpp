#pragma once

#include "repl.hpp"
#include <gtest/gtest.h>
#include <memory>

namespace test_helpers {

/**
 * @brief Mock BuildSettings for testing
 *
 * Provides controlled, predictable BuildSettings for unit tests
 * without depending on external configuration or global state.
 */
class MockBuildSettings {
  public:
    static std::unique_ptr<BuildSettings> create() {
        auto settings = std::make_unique<BuildSettings>();

        // Set default test values
        settings->linkLibraries = {"stdc++", "m"}; // Basic C++ libs
        settings->includeDirectories = {"/usr/include", "/usr/local/include"};
        settings->preprocessorDefinitions = {"TEST_MODE=1"};

        return settings;
    }

    static std::unique_ptr<BuildSettings> createMinimal() {
        auto settings = std::make_unique<BuildSettings>();

        // Minimal configuration for isolated tests
        settings->linkLibraries.clear();
        settings->includeDirectories.clear();
        settings->preprocessorDefinitions.clear();

        return settings;
    }

    static std::unique_ptr<BuildSettings>
    createWithLibraries(const std::vector<std::string> &libraries) {
        auto settings = createMinimal();
        settings->linkLibraries.insert(libraries.begin(), libraries.end());
        return settings;
    }
};

/**
 * @brief Test utilities for string manipulation
 */
class StringTestUtils {
  public:
    /**
     * @brief Check if string contains ANSI color codes
     * @param str String to check
     * @return true if contains ANSI escape sequences
     */
    static bool containsAnsiColors(const std::string &str) {
        return str.find("\033[") != std::string::npos;
    }

    /**
     * @brief Remove ANSI color codes from string
     * @param str String with potential color codes
     * @return Clean string without escape sequences
     */
    static std::string removeAnsiColors(const std::string &str) {
        std::string result = str;
        size_t pos = 0;

        // Remove ANSI escape sequences (\033[...m)
        while ((pos = result.find("\033[", pos)) != std::string::npos) {
            size_t end_pos = result.find("m", pos);
            if (end_pos != std::string::npos) {
                result.erase(pos, end_pos - pos + 1);
            } else {
                break;
            }
        }

        return result;
    }
};

} // namespace test_helpers
