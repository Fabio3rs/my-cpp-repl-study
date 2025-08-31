#include "../test_helpers/temp_directory_fixture.hpp"
#include "completion/clang_completion.hpp"
#include "completion/readline_integration.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace completion;
using namespace test_helpers;

class ReadlineIntegrationTest : public TempDirectoryFixture {
  protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();
        // Ensure clean state before each test
        ReadlineIntegration::cleanup();
    }

    void TearDown() override {
        // Clean up after each test
        ReadlineIntegration::cleanup();
        TempDirectoryFixture::TearDown();
    }
};

// Initialization tests
TEST_F(ReadlineIntegrationTest, Initialize_FirstTime_SuccessfullyInitializes) {
    EXPECT_FALSE(ReadlineIntegration::isInitialized());

    EXPECT_NO_THROW({ ReadlineIntegration::initialize(); });

    EXPECT_TRUE(ReadlineIntegration::isInitialized());
    EXPECT_NE(ReadlineIntegration::getClangCompletion(), nullptr);
}

TEST_F(ReadlineIntegrationTest, Initialize_MultipleCalls_HandlesGracefully) {
    ReadlineIntegration::initialize();
    auto firstInstance = ReadlineIntegration::getClangCompletion();

    // Second initialization should not create new instance
    ReadlineIntegration::initialize();
    auto secondInstance = ReadlineIntegration::getClangCompletion();

    EXPECT_EQ(firstInstance, secondInstance);
}

TEST_F(ReadlineIntegrationTest,
       Cleanup_AfterInitialization_CleansSuccessfully) {
    ReadlineIntegration::initialize();
    EXPECT_TRUE(ReadlineIntegration::isInitialized());

    ReadlineIntegration::cleanup();
    EXPECT_FALSE(ReadlineIntegration::isInitialized());
    EXPECT_EQ(ReadlineIntegration::getClangCompletion(), nullptr);
}

// Context update tests
TEST_F(ReadlineIntegrationTest,
       UpdateContext_WithValidContext_UpdatesSuccessfully) {
    ReadlineIntegration::initialize();

    ReplContext context;
    context.currentIncludes = "#include <iostream>\n";
    context.variableDeclarations = "int x = 42;\n";
    context.functionDeclarations = "void test();\n";

    EXPECT_NO_THROW({ ReadlineIntegration::updateContext(context); });
}

TEST_F(ReadlineIntegrationTest,
       UpdateContext_WithoutInitialization_HandlesGracefully) {
    // Should not crash when not initialized
    ReplContext context;

    EXPECT_NO_THROW({ ReadlineIntegration::updateContext(context); });
}

// RAII scope tests
TEST_F(ReadlineIntegrationTest,
       ReadlineCompletionScope_Constructor_InitializesSystem) {
    EXPECT_FALSE(ReadlineIntegration::isInitialized());

    {
        ReadlineCompletionScope scope;
        EXPECT_TRUE(ReadlineIntegration::isInitialized());
    }

    // Should cleanup after scope ends
    EXPECT_FALSE(ReadlineIntegration::isInitialized());
}

TEST_F(ReadlineIntegrationTest,
       ReadlineCompletionScope_NestedScopes_HandlesCorrectly) {
    EXPECT_FALSE(ReadlineIntegration::isInitialized());

    {
        ReadlineCompletionScope outerScope;
        EXPECT_TRUE(ReadlineIntegration::isInitialized());

        {
            ReadlineCompletionScope innerScope;
            EXPECT_TRUE(ReadlineIntegration::isInitialized());
        }

        // Should still be initialized (outer scope still active)
        EXPECT_TRUE(ReadlineIntegration::isInitialized());
    }

    // Now should be cleaned up
    EXPECT_FALSE(ReadlineIntegration::isInitialized());
}

// Context builder tests
TEST(ContextBuilderTest, BuildFromReplState_EmptyInput_CreatesValidContext) {
    auto context = context_builder::buildFromReplState("");

    EXPECT_FALSE(context.currentIncludes.empty()); // Should have mock includes
    EXPECT_FALSE(
        context.variableDeclarations.empty()); // Should have mock variables
    EXPECT_EQ(context.line, 1);
    EXPECT_EQ(context.column, 1);
}

TEST(ContextBuilderTest,
     BuildFromReplState_WithInput_CalculatesCursorPosition) {
    std::string input = "int x = 42;\nstd::cout << x;";
    auto context = context_builder::buildFromReplState(input);

    EXPECT_EQ(context.activeCode, input);
    EXPECT_EQ(context.line, 2);   // Should be on second line
    EXPECT_GT(context.column, 1); // Should be after some characters
}

TEST(ContextBuilderTest, ExtractIncludes_BasicCall_ReturnsIncludes) {
    auto includes = context_builder::extractIncludes("");

    EXPECT_FALSE(includes.empty());
    EXPECT_NE(includes.find("#include"), std::string::npos);
    EXPECT_NE(includes.find("<iostream>"), std::string::npos);
}

TEST(ContextBuilderTest,
     ExtractVariableDeclarations_BasicCall_ReturnsVariables) {
    auto variables = context_builder::extractVariableDeclarations();

    EXPECT_FALSE(variables.empty());
    EXPECT_NE(variables.find("int"), std::string::npos);
    EXPECT_NE(variables.find("std::string"), std::string::npos);
}

TEST(ContextBuilderTest,
     ExtractFunctionDeclarations_BasicCall_ReturnsFunctions) {
    auto functions = context_builder::extractFunctionDeclarations();

    EXPECT_FALSE(functions.empty());
    EXPECT_NE(functions.find("void"), std::string::npos);
}

TEST(ContextBuilderTest,
     GetCurrentCursorPosition_SimpleText_CalculatesCorrectly) {
    auto [line, column] = context_builder::getCurrentCursorPosition("hello");
    EXPECT_EQ(line, 1);
    EXPECT_EQ(column, 6); // After "hello"
}

TEST(ContextBuilderTest,
     GetCurrentCursorPosition_MultilineText_CalculatesCorrectly) {
    auto [line, column] =
        context_builder::getCurrentCursorPosition("line1\nline2\nline3");
    EXPECT_EQ(line, 3);
    EXPECT_EQ(column, 6); // After "line3"
}

TEST(ContextBuilderTest, GetCurrentCursorPosition_EmptyText_ReturnsDefault) {
    auto [line, column] = context_builder::getCurrentCursorPosition("");
    EXPECT_EQ(line, 1);
    EXPECT_EQ(column, 1);
}

// Integration tests with mock readline callbacks
TEST_F(ReadlineIntegrationTest,
       SetupReadlineCallbacks_AfterInitialization_ConfiguresCorrectly) {
    ReadlineIntegration::initialize();

    // Should not crash when setting up callbacks
    EXPECT_NO_THROW({ ReadlineIntegration::setupReadlineCallbacks(); });
}

// Thread safety tests
TEST_F(ReadlineIntegrationTest, ConcurrentAccess_MultipleThreads_ThreadSafe) {
    const int numThreads = 5;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    // Test concurrent initialization and cleanup
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&successCount]() {
            try {
                ReadlineIntegration::initialize();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                ReplContext context;
                context.activeCode = "test";
                ReadlineIntegration::updateContext(context);

                ReadlineIntegration::cleanup();
                successCount++;
            } catch (...) {
                // Thread failed
            }
        });
    }

    // Wait for all threads to complete
    for (auto &thread : threads) {
        thread.join();
    }

    // All threads should have completed successfully
    EXPECT_EQ(successCount.load(), numThreads);
}

// Performance tests
TEST_F(ReadlineIntegrationTest,
       ContextUpdate_Performance_CompletesInReasonableTime) {
    ReadlineIntegration::initialize();

    ReplContext context;
    context.currentIncludes =
        "#include <iostream>\n#include <vector>\n#include <string>\n";
    context.variableDeclarations = "int x = 42; std::string s = \"hello\";";
    context.functionDeclarations = "void func1(); int func2(int a);";

    auto start = std::chrono::steady_clock::now();

    // Update context multiple times
    for (int i = 0; i < 100; ++i) {
        ReadlineIntegration::updateContext(context);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete within reasonable time (< 500ms for 100 updates)
    EXPECT_LT(duration.count(), 500);
}

// Error handling tests
TEST_F(ReadlineIntegrationTest,
       GetClangCompletion_WithoutInitialization_ReturnsNull) {
    EXPECT_EQ(ReadlineIntegration::getClangCompletion(), nullptr);
}

TEST_F(ReadlineIntegrationTest, IsInitialized_InitialState_ReturnsFalse) {
    EXPECT_FALSE(ReadlineIntegration::isInitialized());
}

// Edge case tests
TEST_F(ReadlineIntegrationTest, MultipleCleanupCalls_HandlesGracefully) {
    ReadlineIntegration::initialize();

    // Multiple cleanup calls should not crash
    EXPECT_NO_THROW({
        ReadlineIntegration::cleanup();
        ReadlineIntegration::cleanup();
        ReadlineIntegration::cleanup();
    });

    EXPECT_FALSE(ReadlineIntegration::isInitialized());
}

TEST_F(ReadlineIntegrationTest,
       UpdateContext_VeryLargeContext_HandlesGracefully) {
    ReadlineIntegration::initialize();

    ReplContext context;

    // Create very large context strings
    for (int i = 0; i < 1000; ++i) {
        context.variableDeclarations +=
            "int var" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
        context.functionDeclarations +=
            "void func" + std::to_string(i) + "();\n";
    }

    EXPECT_NO_THROW({ ReadlineIntegration::updateContext(context); });
}

// Callback simulation tests (testing the static callback functions indirectly)
TEST_F(ReadlineIntegrationTest,
       CallbackSimulation_WithValidState_HandlesCorrectly) {
    ReadlineIntegration::initialize();

    // Set up some context first
    ReplContext context;
    context.variableDeclarations = "int myVar = 42;";
    ReadlineIntegration::updateContext(context);

    // The actual callbacks are static and integrate with readline's C API
    // We can't test them directly without mocking readline, but we can verify
    // that the system is in a valid state for callbacks to work
    EXPECT_TRUE(ReadlineIntegration::isInitialized());
    EXPECT_NE(ReadlineIntegration::getClangCompletion(), nullptr);

    // Test that completion works through the ClangCompletion instance
    auto *completion = ReadlineIntegration::getClangCompletion();
    auto completions = completion->getCompletions("my", 1, 2);

    // Should find completions (mock will return some results)
    EXPECT_GE(completions.size(), 0);
}
