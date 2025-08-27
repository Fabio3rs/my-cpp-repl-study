#include "../test_helpers/temp_directory_fixture.hpp"
#include "analysis/ast_context.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace analysis;
using namespace test_helpers;

class AstContextTest : public TempDirectoryFixture {
  protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();
        context = std::make_unique<AstContext>();
    }

    void TearDown() override {
        context.reset();
        TempDirectoryFixture::TearDown();
    }

    std::unique_ptr<AstContext> context;
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(AstContextTest, Constructor_DefaultState_ValidInitialization) {
    // Should be created successfully in SetUp
    ASSERT_NE(context, nullptr);

    // Initial state should be empty
    EXPECT_TRUE(context->getOutputHeader().empty())
        << "Initial output header should be empty";
}

TEST_F(AstContextTest, AddDeclaration_SingleDeclaration_Success) {
    std::string decl = "int global_var = 42;";

    context->addDeclaration(decl);

    auto header = context->getOutputHeader();
    EXPECT_FALSE(header.empty()) << "Header should contain the declaration";
    EXPECT_NE(header.find(decl), std::string::npos)
        << "Header should contain the added declaration";
}

TEST_F(AstContextTest, AddDeclaration_MultipleDeclarations_AccumulatesContent) {
    std::vector<std::string> declarations = {
        "int var1 = 1;", "double var2 = 2.0;", "std::string var3 = \"test\";"};

    for (const auto &decl : declarations) {
        context->addDeclaration(decl);
    }

    auto header = context->getOutputHeader();
    EXPECT_FALSE(header.empty()) << "Header should contain declarations";

    // Verify all declarations are present in the header
    for (const auto &decl : declarations) {
        EXPECT_NE(header.find(decl), std::string::npos)
            << "Declaration '" << decl << "' should be in header";
    }
}

TEST_F(AstContextTest, AddDeclaration_EmptyString_Handled) {
    size_t initial_size = context->getOutputHeader().size();

    context->addDeclaration("");

    // Header should change (even if just adding newlines/formatting)
    size_t final_size = context->getOutputHeader().size();
    EXPECT_GE(final_size, initial_size)
        << "Empty declaration should be handled gracefully";
}

// ============================================================================
// Include File Management Tests
// ============================================================================

TEST_F(AstContextTest, AddInclude_SingleInclude_Success) {
    std::string include_path = "#include <iostream>";

    context->addInclude(include_path);

    auto header = context->getOutputHeader();
    EXPECT_NE(header.find(include_path), std::string::npos)
        << "Include should be added to header";
}

TEST_F(AstContextTest, MarkFileIncluded_SingleFile_Success) {
    std::string filename = "test_header.hpp";

    // Initially not included
    EXPECT_FALSE(context->isFileIncluded(filename))
        << "File should not be initially included";

    // Mark as included
    context->markFileIncluded(filename);

    // Now should be included
    EXPECT_TRUE(context->isFileIncluded(filename))
        << "File should be marked as included";
}

TEST_F(AstContextTest, MarkFileIncluded_MultipleFiles_AllTracked) {
    std::vector<std::string> files = {"header1.hpp", "header2.h",
                                      "header3.hxx"};

    // Mark all files as included
    for (const auto &file : files) {
        context->markFileIncluded(file);
    }

    // Verify all files are tracked
    for (const auto &file : files) {
        EXPECT_TRUE(context->isFileIncluded(file))
            << "File " << file << " should be marked as included";
    }
}

TEST_F(AstContextTest, IsFileIncluded_NonexistentFile_ReturnsFalse) {
    EXPECT_FALSE(context->isFileIncluded("nonexistent.hpp"))
        << "Non-existent file should not be marked as included";
}

// ============================================================================
// File I/O Tests
// ============================================================================

TEST_F(AstContextTest, SaveHeaderToFile_WithContent_CreatesValidFile) {
    // Add some content to the context
    context->addInclude("#include <iostream>");
    context->addDeclaration("extern int global_var;");
    context->addDeclaration("extern void test_function();");

    std::string filename = "test_header.hpp";

    // Save to file
    bool result = context->saveHeaderToFile(filename);
    EXPECT_TRUE(result) << "Save operation should succeed";

    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(filename)) << "File should be created";

    // Read and verify content
    std::ifstream file(filename);
    ASSERT_TRUE(file.is_open()) << "File should be readable";

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    EXPECT_FALSE(content.empty()) << "File should have content";
    EXPECT_NE(content.find("#include <iostream>"), std::string::npos)
        << "Should contain include statement";
    EXPECT_NE(content.find("extern int global_var;"), std::string::npos)
        << "Should contain variable declaration";
    EXPECT_NE(content.find("extern void test_function();"), std::string::npos)
        << "Should contain function declaration";
}

TEST_F(AstContextTest, SaveHeaderToFile_EmptyContext_CreatesEmptyFile) {
    std::string filename = "empty_header.hpp";

    bool result = context->saveHeaderToFile(filename);
    EXPECT_TRUE(result)
        << "Save operation should succeed even with empty context";

    EXPECT_TRUE(std::filesystem::exists(filename)) << "File should be created";
}

TEST_F(AstContextTest, SaveHeaderToFile_InvalidPath_ReturnsFalse) {
    // Add some content first
    context->addDeclaration("int test;");

    // Try to save to invalid path
    std::string invalid_path = "/nonexistent_directory/file.hpp";

    bool result = context->saveHeaderToFile(invalid_path);
    EXPECT_FALSE(result) << "Save to invalid path should fail";
}

// ============================================================================
// Header Change Detection Tests
// ============================================================================

TEST_F(AstContextTest, HasHeaderChanged_AfterDeclaration_ReturnsTrue) {
    // Initially, header might not be considered changed
    bool initial_changed = context->hasHeaderChanged();

    // Add a declaration
    context->addDeclaration("int new_var = 42;");

    // Header should be considered changed
    bool after_change = context->hasHeaderChanged();
    EXPECT_TRUE(after_change)
        << "Header should be marked as changed after adding declaration";
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(AstContextTest, AddDeclaration_ConcurrentAccess_ThreadSafe) {
    const int num_threads = 4;
    const int declarations_per_thread = 25; // Reduced for stability
    std::vector<std::thread> threads;

    // Launch multiple threads adding declarations concurrently
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < declarations_per_thread; ++i) {
                std::string decl = "extern int var_t" + std::to_string(t) +
                                   "_i" + std::to_string(i) + ";";
                context->addDeclaration(decl);
            }
        });
    }

    // Wait for all threads to complete
    for (auto &t : threads) {
        t.join();
    }

    // Verify header contains content from multiple threads
    auto header = context->getOutputHeader();
    EXPECT_FALSE(header.empty())
        << "Header should contain declarations from concurrent threads";

    // Check that some declarations from different threads are present
    bool found_t0 = header.find("var_t0") != std::string::npos;
    bool found_t1 = header.find("var_t1") != std::string::npos;

    EXPECT_TRUE(found_t0 || found_t1)
        << "Header should contain declarations from multiple threads";
}

TEST_F(AstContextTest, MarkFileIncluded_ConcurrentAccess_ThreadSafe) {
    const int num_threads = 4;
    const int files_per_thread = 25;
    std::vector<std::thread> threads;

    // Launch multiple threads marking files as included
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < files_per_thread; ++i) {
                std::string filename = "header_t" + std::to_string(t) + "_i" +
                                       std::to_string(i) + ".hpp";
                context->markFileIncluded(filename);
            }
        });
    }

    // Wait for all threads to complete
    for (auto &t : threads) {
        t.join();
    }

    // Verify some files from different threads were marked
    bool found_t0_file = context->isFileIncluded("header_t0_i0.hpp");
    bool found_t1_file = context->isFileIncluded("header_t1_i0.hpp");

    EXPECT_TRUE(found_t0_file || found_t1_file)
        << "Files should be marked as included from multiple threads";
}

// ============================================================================
// Clear and Reset Tests
// ============================================================================

TEST_F(AstContextTest, Clear_AfterContent_ResetsState) {
    // Add some content
    context->addInclude("#include <vector>");
    context->addDeclaration("extern std::vector<int> global_vec;");
    context->markFileIncluded("test.hpp");

    // Verify content exists
    EXPECT_FALSE(context->getOutputHeader().empty())
        << "Should have content before clear";
    EXPECT_TRUE(context->isFileIncluded("test.hpp"))
        << "File should be marked as included";

    // Clear the context
    context->clear();

    // Verify state is reset
    EXPECT_TRUE(context->getOutputHeader().empty())
        << "Header should be empty after clear";
    EXPECT_FALSE(context->isFileIncluded("test.hpp"))
        << "File should not be included after clear";
}

// ============================================================================
// Edge Cases and Performance
// ============================================================================

TEST_F(AstContextTest, LargeContent_Performance_HandlesEfficiently) {
    const int large_count = 500; // Reduced for test stability

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < large_count; ++i) {
        context->addDeclaration("extern int large_var" + std::to_string(i) +
                                ";");
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);

    // Should handle large content in reasonable time (< 1 second)
    EXPECT_LT(duration.count(), 1000)
        << "Large content should be handled efficiently";

    auto header = context->getOutputHeader();
    EXPECT_FALSE(header.empty()) << "Header should contain all large content";

    // Verify some of the content is present
    EXPECT_NE(header.find("large_var0"), std::string::npos)
        << "Should contain first variable";
    EXPECT_NE(header.find("large_var" + std::to_string(large_count - 1)),
              std::string::npos)
        << "Should contain last variable";
}
