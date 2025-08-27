#include "../test_helpers/temp_directory_fixture.hpp"
#include "repl.hpp" // For VarDecl definition
#include "utility/library_introspection.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace test_helpers;

class LibraryIntrospectionTest : public TempDirectoryFixture {
  protected:
    void SetUp() override { TempDirectoryFixture::SetUp(); }

    void TearDown() override { TempDirectoryFixture::TearDown(); }

    // Helper to create a simple shared library file for testing
    void createMockLibrary(const std::string &libname) {
        // Create a simple .so file (just for file existence tests)
        // Note: This won't be a real shared library, just a file with .so
        // extension
        std::string filename = "lib" + libname + ".so";
        createFile(filename, "mock_library_content");
    }
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(LibraryIntrospectionTest,
       GetBuiltFileDecls_NonexistentLibrary_ReturnsEmpty) {
    // Try to get declarations from non-existent library
    auto declarations = utility::getBuiltFileDecls("nonexistent_lib");

    // Should return empty vector for non-existent library
    EXPECT_TRUE(declarations.empty())
        << "Non-existent library should return empty declarations";
}

TEST_F(LibraryIntrospectionTest,
       GetBuiltFileDecls_EmptyLibraryName_ReturnsEmpty) {
    // Try with empty library name
    auto declarations = utility::getBuiltFileDecls("");

    // Should return empty vector for empty name
    EXPECT_TRUE(declarations.empty())
        << "Empty library name should return empty declarations";
}

TEST_F(LibraryIntrospectionTest,
       GetBuiltFileDecls_ValidLibraryFile_HandlesGracefully) {
    // Create a mock library file
    createMockLibrary("test_lib");

    // Try to get declarations
    // Note: This will likely fail since it's not a real shared library,
    // but the function should handle it gracefully without crashing
    auto declarations = utility::getBuiltFileDecls("test_lib");

    // Function should not crash and return some result (likely empty)
    // The exact behavior depends on the implementation
    SUCCEED()
        << "Function should handle mock library gracefully without crashing";
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(LibraryIntrospectionTest,
       GetBuiltFileDecls_PermissionDenied_HandlesGracefully) {
    // This test verifies the function handles permission errors gracefully
    // We can't easily create a permission-denied scenario in all environments,
    // so this is more of a documentation test

    auto declarations = utility::getBuiltFileDecls("/root/restricted_lib");

    // Should not crash and return empty or handle error gracefully
    SUCCEED() << "Function should handle permission errors gracefully";
}

// ============================================================================
// Integration Tests with Real Scenarios
// ============================================================================

TEST_F(LibraryIntrospectionTest,
       GetBuiltFileDecls_SystemLibrary_HandlesExisting) {
    // Test with a common system library that likely exists
    // Note: This test might behave differently on different systems

    auto declarations = utility::getBuiltFileDecls("c"); // libc

    // The function should not crash, regardless of what it returns
    // On some systems it might return declarations, on others it might be empty
    SUCCEED() << "Function should handle system libraries without crashing";
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(LibraryIntrospectionTest,
       GetBuiltFileDecls_Performance_CompletesInReasonableTime) {
    // Create several mock libraries
    std::vector<std::string> lib_names = {"lib1", "lib2", "lib3", "lib4",
                                          "lib5"};

    for (const auto &lib : lib_names) {
        createMockLibrary(lib);
    }

    auto start_time = std::chrono::steady_clock::now();

    // Try to introspect multiple libraries
    for (const auto &lib : lib_names) {
        auto declarations = utility::getBuiltFileDecls(lib);
        (void)declarations; // Use the result to prevent optimization
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    // Should complete in reasonable time (< 5 seconds for 5 libraries)
    EXPECT_LT(duration.count(), 5000)
        << "Library introspection should complete in reasonable time";
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(LibraryIntrospectionTest, GetBuiltFileDecls_ConcurrentCalls_ThreadSafe) {
    // Create mock libraries for concurrent testing
    createMockLibrary("concurrent_lib1");
    createMockLibrary("concurrent_lib2");

    const int num_threads = 3;
    std::vector<std::thread> threads;
    std::vector<bool> thread_results(num_threads, false);

    // Launch concurrent calls
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            try {
                auto declarations1 =
                    utility::getBuiltFileDecls("concurrent_lib1");
                auto declarations2 =
                    utility::getBuiltFileDecls("concurrent_lib2");

                // If we get here without crashing, the call was thread-safe
                thread_results[t] = true;

                (void)declarations1; // Use results to prevent optimization
                (void)declarations2;
            } catch (...) {
                // Catch any exceptions to prevent test crashes
                thread_results[t] = false;
            }
        });
    }

    // Wait for all threads to complete
    for (auto &t : threads) {
        t.join();
    }

    // All threads should have completed successfully
    for (int t = 0; t < num_threads; ++t) {
        EXPECT_TRUE(thread_results[t])
            << "Thread " << t << " should complete without exceptions";
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(LibraryIntrospectionTest,
       GetBuiltFileDecls_VeryLongLibraryName_HandlesGracefully) {
    // Test with very long library name
    std::string long_name(1000, 'a'); // 1000 character name

    auto declarations = utility::getBuiltFileDecls(long_name);

    // Should handle long names gracefully without crashing
    SUCCEED() << "Function should handle very long library names gracefully";
}

TEST_F(LibraryIntrospectionTest,
       GetBuiltFileDecls_SpecialCharacters_HandlesGracefully) {
    // Test with library names containing special characters
    std::vector<std::string> special_names = {
        "lib-with-dashes", "lib_with_underscores", "lib.with.dots",
        "lib with spaces", // This will likely fail, but shouldn't crash
        "lib@#$%^&*()"     // This will likely fail, but shouldn't crash
    };

    for (const auto &name : special_names) {
        auto declarations = utility::getBuiltFileDecls(name);
        // Just verify it doesn't crash
        (void)declarations;
    }

    SUCCEED() << "Function should handle special characters gracefully";
}

TEST_F(LibraryIntrospectionTest,
       GetBuiltFileDecls_NullOrEmptyInput_ReturnsEmpty) {
    // Test various forms of empty/null input

    auto result1 = utility::getBuiltFileDecls("");
    EXPECT_TRUE(result1.empty()) << "Empty string should return empty result";

    auto result2 = utility::getBuiltFileDecls("   "); // Whitespace only
    EXPECT_TRUE(result2.empty())
        << "Whitespace-only string should return empty result";
}
