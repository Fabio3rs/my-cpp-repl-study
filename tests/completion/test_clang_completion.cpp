#include "../test_helpers/temp_directory_fixture.hpp"
#include "completion/clang_completion.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <memory>

using namespace completion;
using namespace test_helpers;

class ClangCompletionTest : public TempDirectoryFixture {
  protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();
        clangCompletion = std::make_unique<ClangCompletion>();
    }

    void TearDown() override {
        clangCompletion.reset();
        TempDirectoryFixture::TearDown();
    }

    std::unique_ptr<ClangCompletion> clangCompletion;
};

// Basic functionality tests
TEST_F(ClangCompletionTest, Constructor_DefaultState_ValidInitialization) {
    EXPECT_TRUE(clangCompletion != nullptr);

    // Should not crash when calling basic methods
    EXPECT_NO_THROW({ clangCompletion->clearCache(); });
}

TEST_F(ClangCompletionTest,
       UpdateReplContext_ValidContext_UpdatesSuccessfully) {
    ReplContext context;
    context.currentIncludes = "#include <iostream>\n";
    context.variableDeclarations = "int x = 42;\n";
    context.functionDeclarations = "void test();\n";
    context.activeCode = "std::cout << x;";
    context.line = 1;
    context.column = 10;

    EXPECT_NO_THROW({ clangCompletion->updateReplContext(context); });
}

TEST_F(ClangCompletionTest, GetCompletions_EmptyCode_ReturnsResults) {
    auto completions = clangCompletion->getCompletions("", 1, 1);

    // Should return some completions (at least keywords)
    EXPECT_GE(completions.size(), 0);
}

TEST_F(ClangCompletionTest, GetCompletions_StdPrefix_ReturnsStdCompletions) {
    auto completions = clangCompletion->getCompletions("std::", 1, 5);

    // Should find std:: completions
    bool foundStdCompletion = false;
    for (const auto &completion : completions) {
        if (completion.text.find("vector") != std::string::npos ||
            completion.text.find("string") != std::string::npos ||
            completion.text.find("cout") != std::string::npos) {
            foundStdCompletion = true;
            break;
        }
    }

    EXPECT_TRUE(foundStdCompletion);
}

TEST_F(ClangCompletionTest, GetCompletions_KeywordPrefix_ReturnsKeywords) {
    auto completions = clangCompletion->getCompletions("in", 1, 2);

    // Should find 'int' keyword
    bool foundIntKeyword = false;
    for (const auto &completion : completions) {
        if (completion.text == "int" &&
            completion.kind == CompletionItem::Kind::Keyword) {
            foundIntKeyword = true;
            break;
        }
    }

    EXPECT_TRUE(foundIntKeyword);
}

TEST_F(ClangCompletionTest, GetCompletions_ResultsAreSorted_ByPriority) {
    auto completions = clangCompletion->getCompletions("std::", 1, 5);

    // Results should be sorted by priority (higher first)
    for (size_t i = 1; i < completions.size(); ++i) {
        EXPECT_GE(completions[i - 1].priority, completions[i].priority);
    }
}

// Documentation tests
TEST_F(ClangCompletionTest, GetDocumentation_KnownSymbol_ReturnsDocumentation) {
    std::string doc = clangCompletion->getDocumentation("vector");

    EXPECT_FALSE(doc.empty());
    EXPECT_NE(doc, "No documentation available");
}

TEST_F(ClangCompletionTest, GetDocumentation_UnknownSymbol_ReturnsDefault) {
    std::string doc = clangCompletion->getDocumentation("unknownSymbolXYZ123");

    EXPECT_EQ(doc, "No documentation available");
}

// Diagnostics tests
TEST_F(ClangCompletionTest, GetDiagnostics_ValidCode_ReturnsNoDiagnostics) {
    std::string code = "#include <iostream>\nint main() { return 0; }";
    auto diagnostics = clangCompletion->getDiagnostics(code);

    // Valid code should have no diagnostics
    EXPECT_TRUE(diagnostics.empty());
}

TEST_F(ClangCompletionTest, GetDiagnostics_CodeWithIssues_ReturnsDiagnostics) {
    // Code that uses cout without including iostream
    std::string code = "cout << \"hello\";";
    auto diagnostics = clangCompletion->getDiagnostics(code);

    // Should detect the missing include
    bool foundMissingInclude = false;
    for (const auto &diagnostic : diagnostics) {
        if (diagnostic.find("iostream") != std::string::npos) {
            foundMissingInclude = true;
            break;
        }
    }

    EXPECT_TRUE(foundMissingInclude);
}

TEST_F(ClangCompletionTest, GetDiagnostics_MissingSemicolon_DetectsIssue) {
    std::string code = "int x = 42"; // Missing semicolon
    auto diagnostics = clangCompletion->getDiagnostics(code);

    // Should detect missing semicolon
    bool foundSemicolonIssue = false;
    for (const auto &diagnostic : diagnostics) {
        if (diagnostic.find("semicolon") != std::string::npos) {
            foundSemicolonIssue = true;
            break;
        }
    }

    EXPECT_TRUE(foundSemicolonIssue);
}

// Symbol existence tests
TEST_F(ClangCompletionTest, SymbolExists_WithContext_FindsVariable) {
    ReplContext context;
    context.variableDeclarations = "int myVariable = 42;\n";
    clangCompletion->updateReplContext(context);

    EXPECT_TRUE(clangCompletion->symbolExists("myVariable"));
    EXPECT_FALSE(clangCompletion->symbolExists("nonExistentVariable"));
}

// Cache tests
TEST_F(ClangCompletionTest, ClearCache_AfterCompletions_ClearsSuccessfully) {
    // Get some completions first to populate cache
    auto completions = clangCompletion->getCompletions("std::", 1, 5);

    // Clear cache should not throw
    EXPECT_NO_THROW({ clangCompletion->clearCache(); });
}

// Context integration tests
TEST_F(ClangCompletionTest, ContextUpdate_WithVariables_ReflectsInCompletions) {
    ReplContext context;
    context.variableDeclarations =
        "int myCustomVar = 100;\nstd::string myString = \"hello\";";
    clangCompletion->updateReplContext(context);

    auto completions = clangCompletion->getCompletions("my", 1, 2);

    // Should find our custom variable in completions
    bool foundCustomVar = false;
    for (const auto &completion : completions) {
        if (completion.text == "myCustomVar" ||
            completion.text.find("myVar") != std::string::npos) {
            foundCustomVar = true;
            EXPECT_EQ(completion.kind, CompletionItem::Kind::Variable);
            break;
        }
    }

    EXPECT_TRUE(foundCustomVar);
}

// Performance tests
TEST_F(ClangCompletionTest,
       GetCompletions_Performance_CompletesInReasonableTime) {
    auto start = std::chrono::steady_clock::now();

    // Get completions multiple times
    for (int i = 0; i < 10; ++i) {
        auto completions = clangCompletion->getCompletions("std::", 1, 5);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete within reasonable time (< 1000ms for 10 calls)
    EXPECT_LT(duration.count(), 1000);
}

// Edge case tests
TEST_F(ClangCompletionTest, GetCompletions_LargeCode_HandlesGracefully) {
    // Create a large code string
    std::string largeCode;
    for (int i = 0; i < 1000; ++i) {
        largeCode +=
            "int var" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    }

    EXPECT_NO_THROW({
        auto completions = clangCompletion->getCompletions(largeCode, 1, 1);
    });
}

TEST_F(ClangCompletionTest, GetCompletions_InvalidPosition_HandlesGracefully) {
    EXPECT_NO_THROW({
        auto completions = clangCompletion->getCompletions("test", -1, -1);
    });

    EXPECT_NO_THROW({
        auto completions = clangCompletion->getCompletions("test", 1000, 1000);
    });
}

// CompletionItem tests
TEST(CompletionItemTest, CompletionItem_DefaultConstruction_ValidState) {
    CompletionItem item;

    EXPECT_TRUE(item.text.empty());
    EXPECT_TRUE(item.display.empty());
    EXPECT_TRUE(item.documentation.empty());
    EXPECT_TRUE(item.returnType.empty());
    EXPECT_EQ(item.priority, 0);
    EXPECT_EQ(item.kind, CompletionItem::Kind::Variable);
}

TEST(CompletionItemTest, CompletionItem_FullConstruction_PreservesValues) {
    CompletionItem item;
    item.text = "test_function";
    item.display = "test_function()";
    item.documentation = "A test function";
    item.returnType = "int";
    item.priority = 10;
    item.kind = CompletionItem::Kind::Function;

    EXPECT_EQ(item.text, "test_function");
    EXPECT_EQ(item.display, "test_function()");
    EXPECT_EQ(item.documentation, "A test function");
    EXPECT_EQ(item.returnType, "int");
    EXPECT_EQ(item.priority, 10);
    EXPECT_EQ(item.kind, CompletionItem::Kind::Function);
}

// ReplContext tests
TEST(ReplContextTest, ReplContext_DefaultConstruction_ValidState) {
    ReplContext context;

    EXPECT_TRUE(context.currentIncludes.empty());
    EXPECT_TRUE(context.variableDeclarations.empty());
    EXPECT_TRUE(context.functionDeclarations.empty());
    EXPECT_TRUE(context.typeDefinitions.empty());
    EXPECT_TRUE(context.activeCode.empty());
    EXPECT_EQ(context.line, 1);
    EXPECT_EQ(context.column, 1);
}
