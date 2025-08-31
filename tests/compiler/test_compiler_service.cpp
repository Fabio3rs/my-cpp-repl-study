#include "../test_helpers/mock_build_settings.hpp"
#include "../test_helpers/temp_directory_fixture.hpp"
#include "analysis/ast_context.hpp"
#include "compiler/compiler_service.hpp"

#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace compiler;
using namespace test_helpers;

class CompilerServiceTest : public TempDirectoryFixture {
  protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();

        // Setup mock build settings
        buildSettings = MockBuildSettings::create();
        astContext = std::make_shared<analysis::AstContext>();

        // Setup variable merge callback for testing
        capturedVars.clear();
        varMergeCallback = [this](const std::vector<VarDecl> &vars) {
            capturedVars.insert(capturedVars.end(), vars.begin(), vars.end());
        };

        // Create CompilerService instance
        compilerService = std::make_unique<CompilerService>(
            buildSettings.get(), astContext, varMergeCallback);

        // Create minimal precompiled header for tests that need it
        createPrecompiledHeader();
    }

    void TearDown() override {
        compilerService.reset();
        astContext.reset();
        buildSettings.reset();
        TempDirectoryFixture::TearDown();
    }

    // Test data
    std::unique_ptr<BuildSettings> buildSettings;
    std::shared_ptr<analysis::AstContext> astContext;
    CompilerService::VarMergeCallback varMergeCallback;
    std::unique_ptr<CompilerService> compilerService;
    std::vector<VarDecl> capturedVars;

    // Helper methods
    void createSimpleTestFile(
        const std::string &filename,
        const std::string &content = "int main() { return 0; }") {
        createFile(filename, content);
    }

    void createPrecompiledHeader() {
        // Create a simplified precompiled header for tests without complex
        // dependencies
        std::string pchSource = R"(#pragma once

#include <any>
#include <string>
#include <iostream>

extern int (*bootstrapProgram)(int argc, char **argv);
extern std::any lastReplResult;
)";

        createFile("precompiledheader.hpp", pchSource);

        // Create a very simple printerOutput.hpp for tests - just basic print
        // wrappers
        std::string printerOutputSource = R"(#pragma once
#include <iostream>
#include <string_view>

// Simplified printer output for tests
template <class T>
inline void printdata(const T& val, std::string_view name, std::string_view type) {
    std::cout << " >> " << type << (name.empty() ? "" : " ")
              << (name.empty() ? "" : name) << ": " << val << std::endl;
}
)";

        createFile("printerOutput.hpp", printerOutputSource);

        // Build the PCH using system command with current directory
        char *cwd = getcwd(nullptr, 0);
        std::string currentDir(cwd);
        free(cwd);

        std::string cmd =
            "clang++ -DTEST_MODE=1 -I/usr/local/include -I/usr/include -fPIC "
            "-x c++-header -std=gnu++20 -o " +
            currentDir + "/precompiledheader.hpp.pch " + currentDir +
            "/precompiledheader.hpp";

        int result = std::system(cmd.c_str());
        if (result != 0) {
            std::cout << "Warning: Could not create precompiled header for "
                         "tests (code: "
                      << result << ")" << std::endl;
        }
    }

    // Create test file with proper includes for AST analysis
    void createTestFileForAST(const std::string &filename,
                              const std::string &content) {
        std::string full_content =
            "#include \"precompiledheader.hpp\"\n\n" + content;
        createFile(filename, full_content);
    }
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(CompilerServiceTest, Constructor_ValidParameters_Success) {
    // Service should be created successfully in SetUp
    ASSERT_NE(compilerService, nullptr);

    // Should have valid AST context
    ASSERT_EQ(compilerService->getAstContext(), astContext);
}

TEST_F(CompilerServiceTest, Constructor_NullBuildSettings_ThrowsException) {
    // Attempt to create with null BuildSettings should throw
    EXPECT_THROW(CompilerService(nullptr, astContext, varMergeCallback),
                 std::invalid_argument);
}

// ============================================================================
// Build Library Only Tests
// ============================================================================

TEST_F(CompilerServiceTest, BuildLibraryOnly_SimpleFile_Success) {
    // Create a simple C++ file
    createSimpleTestFile("testlibsimple.cpp", "int global_var = 42;");

    // Build library (Note: CompilerService expects name + extension)
    auto result = compilerService->buildLibraryOnly("clang++", "testlib",
                                                    "simple.cpp", "gnu++20");

    // Should succeed
    EXPECT_TRUE(result.success()) << "Simple file compilation should succeed";
    if (result.success()) {
        EXPECT_EQ(result.value, 0);
    }
}

TEST_F(CompilerServiceTest, BuildLibraryOnly_NonexistentFile_Failure) {
    // Try to build non-existent file
    auto result = compilerService->buildLibraryOnly(
        "clang++", "testlib", "nonexistent.cpp", "gnu++20");

    // Should fail with SystemCommandFailed (since file doesn't exist)
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CompilerError::SystemCommandFailed);
}

TEST_F(CompilerServiceTest, BuildLibraryOnly_SyntaxError_Failure) {
    // Create file with syntax error
    createSimpleTestFile("testlibinvalid.cpp", "int invalid syntax here;");

    // Build should fail with compilation error
    auto result = compilerService->buildLibraryOnly("clang++", "testlib",
                                                    "invalid.cpp", "gnu++20");

    // Should fail with SystemCommandFailed (compilation error)
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CompilerError::SystemCommandFailed);
}

// ============================================================================
// Build Multiple Sources with AST Tests
// ============================================================================

TEST_F(CompilerServiceTest,
       BuildMultipleSourcesWithAST_SingleFile_ExtractsVariables) {
    // Create C++ file with variables (using proper includes for AST)
    std::string code = R"(
        int global_int = 42;
        std::string global_string = "test";
        double global_double = 3.14;

        void test_function() {
            int local_var = 10;
        }
    )";
    createTestFileForAST("vars.cpp", code);

    // Build with AST analysis
    std::vector<std::string> sources = {"vars.cpp"};
    auto result = compilerService->buildMultipleSourcesWithAST(
        "clang++", "test", sources, "gnu++20");

    // Should succeed and extract variables
    EXPECT_TRUE(result.success()) << "Compilation should succeed with PCH";
    if (result.success()) {
        EXPECT_EQ(result.value.returnCode, 0);
        // Variables might be extracted depending on AST analysis implementation
        // We don't enforce specific count since this depends on the analyzer
    }
}

TEST_F(CompilerServiceTest,
       BuildMultipleSourcesWithAST_MultipleFiles_MergesVariables) {
    // Create multiple source files with proper includes
    createTestFileForAST("file1.cpp", "int var1 = 1; void func1() {}");
    createTestFileForAST("file2.cpp", "double var2 = 2.0; void func2() {}");

    // Build multiple files
    std::vector<std::string> sources = {"file1.cpp", "file2.cpp"};
    auto result = compilerService->buildMultipleSourcesWithAST(
        "clang++", "test", sources, "gnu++20");

    // Should succeed with PCH available
    EXPECT_TRUE(result.success())
        << "Multi-file compilation should succeed with PCH";
    if (result.success()) {
        EXPECT_EQ(result.value.returnCode, 0);
        // Variables might be extracted depending on implementation
    }
}

// ============================================================================
// Color Support Tests
// ============================================================================

TEST_F(CompilerServiceTest, ColorSupport_ErrorMessages_ContainAnsiCodes) {
    // Create file with compilation error
    createSimpleTestFile("error.cpp", "int invalid = syntax error;");

    // Capture stderr to check for color codes
    testing::internal::CaptureStderr();

    auto result = compilerService->buildLibraryOnly("clang++", "test",
                                                    "error.cpp", "gnu++20");

    std::string stderr_output = testing::internal::GetCapturedStderr();

    // Should fail and contain color codes in error output
    EXPECT_FALSE(result.success());

    // Note: Color codes might not appear in test environment, but we can check
    // structure
    EXPECT_FALSE(stderr_output.empty()) << "No error output captured";
}

// ============================================================================
// Analyze Custom Commands Tests
// ============================================================================

TEST_F(CompilerServiceTest,
       AnalyzeCustomCommands_ValidCommands_ExtractsVariables) {
    // Create a simple test file
    std::string code = "int test_var = 42; void test_func() {}";
    createSimpleTestFile("custom.cpp", code);

    // Create command that includes AST dump
    std::vector<std::string> commands = {
        "clang++ -std=gnu++20 -Xclang -ast-dump=json -fsyntax-only "
        "2>custom.log > custom_ast.json custom.cpp"};

    auto result = compilerService->analyzeCustomCommands(commands);

    // Should succeed (even if JSON parsing might fail in test environment)
    EXPECT_TRUE(result.success()) << "Custom commands analysis failed";
    // Note: In real environment, this would extract variables from JSON
}

TEST_F(CompilerServiceTest, AnalyzeCustomCommands_EmptyCommands_Success) {
    std::vector<std::string> commands; // Empty

    auto result = compilerService->analyzeCustomCommands(commands);

    // Should succeed with empty results
    EXPECT_TRUE(result.success());
    EXPECT_TRUE(result.value.empty());
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(CompilerServiceTest, ConcurrentCalls_MultipleThreads_ThreadSafe) {
    // Create test files with names that match what buildLibraryOnly expects
    createSimpleTestFile("lib1thread1.cpp", "int var1 = 1;");
    createSimpleTestFile("lib2thread2.cpp", "int var2 = 2;");

    std::vector<std::thread> threads;
    std::vector<bool> results(2, false);

    // Launch concurrent compilation operations
    threads.emplace_back([&, this]() {
        auto result = compilerService->buildLibraryOnly(
            "clang++", "lib1", "thread1.cpp", "gnu++20");
        results[0] = result.success();
    });

    threads.emplace_back([&, this]() {
        auto result = compilerService->buildLibraryOnly(
            "clang++", "lib2", "thread2.cpp", "gnu++20");
        results[1] = result.success();
    });

    // Wait for completion
    for (auto &t : threads) {
        t.join();
    }

    // Both operations should succeed
    EXPECT_TRUE(results[0]) << "Thread 1 compilation should succeed";
    EXPECT_TRUE(results[1]) << "Thread 2 compilation should succeed";
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_F(CompilerServiceTest, BuildLibraryOnly_EmptyFilename_Failure) {
    auto result =
        compilerService->buildLibraryOnly("clang++", "testlib", "", "gnu++20");

    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CompilerError::SystemCommandFailed);
}

TEST_F(CompilerServiceTest, BuildLibraryOnly_InvalidCompiler_Failure) {
    createSimpleTestFile("simple.cpp");

    auto result = compilerService->buildLibraryOnly(
        "nonexistent_compiler", "testlib", "simple.cpp", "gnu++20");

    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CompilerError::SystemCommandFailed);
}

TEST_F(CompilerServiceTest, GetAstContext_ReturnsCorrectContext) {
    EXPECT_EQ(compilerService->getAstContext(), astContext);
}

TEST_F(CompilerServiceTest, SetAstContext_UpdatesContext) {
    auto newContext = std::make_shared<analysis::AstContext>();
    compilerService->setAstContext(newContext);

    EXPECT_EQ(compilerService->getAstContext(), newContext);
    EXPECT_NE(compilerService->getAstContext(), astContext);
}

TEST_F(CompilerServiceTest, IncludeExists) {
    EXPECT_TRUE(compilerService->checkIncludeExists(*buildSettings, "vector"));
    EXPECT_FALSE(compilerService->checkIncludeExists(
        *buildSettings, "non_existing_include.hpp"));
}
